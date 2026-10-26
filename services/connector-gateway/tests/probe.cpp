#include <psim/connector/v1/connector.pb.h>
#include <psim/ingest/v1/raw_event.pb.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <print>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/connector/client.hpp"
#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "support.hpp"

namespace psim::connector_gateway::testing {
namespace {
p::kafka::ClientOptions reader_options() {
  p::kafka::ClientOptions options{
      .bootstrap = "kafka-1:19092", .client_id = "gateway-probe-reader", .allow_plaintext = true};
  if (probe_configuration().contains("kafka")) {
    const auto& kafka = probe_configuration().at("kafka");
    options.bootstrap = kafka.at("brokers").get<std::string>();
    options.allow_plaintext = false;
    options.security =
        p::kafka::TransportSecurity{.tls = {.ca_file = kafka.at("ca_file").get<std::string>(),
                                            .certificate_file = kafka.at("certificate_file").get<std::string>(),
                                            .private_key_file = kafka.at("private_key_file").get<std::string>()}};
  }
  return options;
}

asio::awaitable<void> fence(std::string identity, p::async::BlockingPool* blocking) {
  const auto executor = co_await asio::this_coro::executor;
  auto producer = p::kafka::Producer::create(
      executor, {.client = reader_options(), .transactional_id = std::move(identity)}, {.blocking = blocking});
  if (!producer) {
    throw std::runtime_error("qualification producer unavailable");
  }
  auto initialized = co_await (*producer)->init_transactions(std::chrono::seconds(10));
  if (!initialized) {
    throw std::runtime_error("qualification producer fencing failed");
  }
  std::println("Transactional producer epoch advanced without publishing data");
}

asio::awaitable<void> verify(std::set<std::string> expected) {
  const auto total = expected.size();
  auto executor = co_await asio::this_coro::executor;
  auto options = reader_options();
  auto consumer = p::kafka::Consumer::create(executor, {.client = std::move(options),
                                                        .group_id = "gateway-probe-" + p::stream::uuid7(),
                                                        .topics = {"psim.ingest.raw.v1"},
                                                        .group_protocol = "classic"});
  if (!consumer) {
    throw std::runtime_error("probe consumer unavailable");
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  std::size_t records = 0;
  while (!expected.empty() && std::chrono::steady_clock::now() < deadline) {
    auto batch = co_await (*consumer)->poll(10000, std::chrono::milliseconds(100));
    if (!batch) {
      throw std::runtime_error("probe consumer failed");
    }
    for (const auto& item : *batch) {
      auto value = p::kafka::ProtobufSerde<ingest::v1::RawEventRecord>(1).deserialize(item.value());
      if (value && expected.erase(value->envelope().message_id()) != 0) {
        ++records;
      }
    }
  }
  co_await (*consumer)->close();
  std::println("gateway reconciliation: expected={}, found={}, missing={}", total, records, expected.size());
  if (!expected.empty()) {
    throw std::runtime_error("gateway reconciliation found missing events");
  }
}

void bench(std::size_t per_client, bool require_rate = true) {
  constexpr int kClients = 16;
  for (int index = 0; index < kClients; ++index) {
    enroll(index, 1000000, std::chrono::seconds(20));
  }
  std::chrono::steady_clock::time_point started;
  std::barrier gate(kClients + 1, [&started]() noexcept { started = std::chrono::steady_clock::now(); });
  std::vector<std::chrono::steady_clock::time_point> completed(kClients);
  std::vector<std::jthread> workers;
  std::mutex mutex;
  std::vector<std::string> errors;
  std::set<std::string> expected;
  std::atomic_size_t accepted{0};
  workers.reserve(kClients);
  for (int index = 0; index < kClients; ++index) {
    workers.emplace_back([&, index] {
      bool waiting = true;
      try {
        Stream stream(index, index % 2);
        if (!stream.connect(false)) {
          throw std::runtime_error("load session registration failed");
        }
        std::vector<wire::ConnectorEvent> events;
        events.reserve(per_client);
        for (std::size_t n = 0; n < per_client; ++n) {
          events.push_back(event(n + 1));
        }
        {
          const std::scoped_lock lock(mutex);
          for (const auto& value : events) {
            expected.insert(value.event_id());
          }
        }
        waiting = false;
        gate.arrive_and_wait();
        auto heartbeat = std::chrono::steady_clock::now();
        for (std::size_t offset = 0; offset < events.size(); offset += 1000) {
          const auto last = std::min(offset + 1000, events.size());
          auto ack =
              stream.batch(std::vector<wire::ConnectorEvent>(events.begin() + static_cast<std::ptrdiff_t>(offset),
                                                             events.begin() + static_cast<std::ptrdiff_t>(last)));
          if (ack.rejections_size() != 0 || ack.accepted() != last - offset) {
            throw std::runtime_error("load batch rejected");
          }
          accepted.fetch_add(ack.accepted());
          completed.at(static_cast<std::size_t>(index)) = std::chrono::steady_clock::now();
          if (std::chrono::steady_clock::now() - heartbeat >= std::chrono::seconds(2)) {
            if (!stream.heartbeat()) {
              throw std::runtime_error("load heartbeat failed");
            }
            heartbeat = std::chrono::steady_clock::now();
          }
        }
      } catch (const std::exception& error) {
        {
          const std::scoped_lock lock(mutex);
          errors.emplace_back(error.what());
        }
        if (waiting) {
          gate.arrive_and_drop();
        }
      }
    });
  }
  gate.arrive_and_wait();
  workers.clear();
  // Measure durable acknowledgements independently of client teardown and fixture destruction.
  const auto finished = std::max(started, std::ranges::max(completed));
  const auto seconds = std::max(0.001, std::chrono::duration<double>(finished - started).count());
  const auto rate = static_cast<double>(accepted.load()) / seconds;
  std::println("gateway benchmark: clients={}, instances=2, accepted={}, seconds={:.3f}, events_per_second={:.0f}",
               kClients, accepted.load(), seconds, rate);
  for (const auto& error : errors) {
    std::println(stderr, "load failure: {}", error);
  }
  if (!errors.empty() || accepted.load() != per_client * kClients) {
    throw std::runtime_error("gateway load did not acknowledge every event");
  }
  run(verify(std::move(expected)));
  if (require_rate && rate < 50000) {
    throw std::runtime_error("gateway rate is below 50000 events/s");
  }
}

void smoke() {
  const auto directory = std::filesystem::path(fixture_directory()) / ("sdk-" + p::stream::uuid7());
  p::connector::Client client({.endpoint = probe_configuration().value("sdk_endpoint", "toxiproxy:18080"),
                               .ca_file = tls_directory() + "/ca.pem",
                               .certificate_file = prefix(19) + ".pem",
                               .private_key_file = prefix(19) + ".key",
                               .buffer_directory = directory.string()});
  auto started = client.start();
  if (!started) {
    throw std::runtime_error(started.error().message());
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (!client.diagnostics().connected && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!client.diagnostics().connected) {
    throw std::runtime_error("qualification SDK did not establish a session");
  }
  wire::ConnectorEvent input;
  input.set_source_ref("door/alarm");
  input.set_raw_code("ALARM");
  *input.mutable_occurred_at() = Util::GetCurrentTime();
  auto published = client.publish(std::move(input));
  if (!published) {
    throw std::runtime_error(published.error().message());
  }
  if (!client.flush(std::chrono::seconds(30))) {
    throw std::runtime_error("qualification event was not durably acknowledged: " + client.diagnostics().last_error);
  }
  const auto diagnostics = client.diagnostics();
  client.stop();
  if (diagnostics.acknowledged != 1 || diagnostics.rejected != 0 || diagnostics.pending_events != 0) {
    throw std::runtime_error("qualification SDK acknowledgement invariants failed");
  }
  std::ofstream output(std::filesystem::path(fixture_directory()) / "event.json");
  output << nlohmann::json{{"event_id", published->event_id()}}.dump() << '\n';
  if (!output) {
    throw std::runtime_error("cannot write qualification event identity");
  }
  std::println("Connector SDK event durably acknowledged with no pending or rejected events");
}

void failover() {
  enroll(18, 1000000, std::chrono::seconds(20));
  const auto directory = std::filesystem::path(fixture_directory()) / ("sdk-" + p::stream::uuid7());
  p::connector::Client client({.endpoint = probe_configuration().value("sdk_endpoint", "toxiproxy:18080"),
                               .ca_file = tls_directory() + "/ca.pem",
                               .certificate_file = prefix(18) + ".pem",
                               .private_key_file = prefix(18) + ".key",
                               .buffer_directory = directory.string(),
                               .reconnect_min = std::chrono::milliseconds(2000),
                               .reconnect_max = std::chrono::milliseconds(3000)});
  auto started = client.start();
  if (!started) {
    throw std::runtime_error(started.error().message());
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (!client.diagnostics().connected && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!client.diagnostics().connected) {
    throw std::runtime_error("SDK did not establish initial session");
  }
  std::println("READY");
  if (std::fflush(stdout) != 0) {
    throw std::runtime_error("cannot report SDK readiness");
  }
  std::set<std::string> expected;
  for (int i = 0; i < 1000; ++i) {
    wire::ConnectorEvent input;
    input.set_source_ref("failure/alarm");
    input.set_raw_code("ALARM");
    *input.mutable_occurred_at() = Util::GetCurrentTime();
    auto stored = client.publish(std::move(input));
    if (!stored) {
      throw std::runtime_error(stored.error().message());
    }
    expected.insert(stored->event_id());
    std::this_thread::sleep_for(std::chrono::milliseconds(probe_configuration().value("failover_interval_ms", 1)));
  }
  if (!client.flush(std::chrono::seconds(45))) {
    throw std::runtime_error("SDK failed to drain through gateway failover: " + client.diagnostics().last_error);
  }
  const auto diagnostics = client.diagnostics();
  client.stop();
  std::println("gateway SDK failover: sessions={}, acknowledged={}, rejected={}, pending={}", diagnostics.sessions,
               diagnostics.acknowledged, diagnostics.rejected, diagnostics.pending_events);
  if (diagnostics.sessions < 2 || diagnostics.acknowledged != 1000 || diagnostics.rejected != 0
      || diagnostics.pending_events != 0) {
    throw std::runtime_error("SDK failover invariants failed");
  }
  run(verify(std::move(expected)));
}
}  // namespace
}  // namespace psim::connector_gateway::testing

int main(int argc, char** argv) {
  try {
    const psim::platform::connector::GrpcProcess grpc;
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (argc < 2) {
      throw std::runtime_error("usage: gateway-probe bench [events-per-client] | failover | enroll | smoke | fence id");
    }
    if (std::string_view(args[1]) == "bench") {
      psim::connector_gateway::testing::bench(argc > 2 ? std::stoull(args[2]) : 25000);
    } else if (std::string_view(args[1]) == "emit") {
      psim::connector_gateway::testing::bench(argc > 2 ? std::stoull(args[2]) : 100, false);
    } else if (std::string_view(args[1]) == "enroll") {
      (void)psim::connector_gateway::testing::enroll(19, 1000000, std::chrono::seconds(20));
    } else if (std::string_view(args[1]) == "smoke") {
      psim::connector_gateway::testing::smoke();
    } else if (std::string_view(args[1]) == "fence" && argc == 3) {
      psim::platform::async::BlockingPool blocking(1, 4);
      psim::connector_gateway::testing::run(psim::connector_gateway::testing::fence(args[2], &blocking));
    } else if (std::string_view(args[1]) == "failover") {
      psim::connector_gateway::testing::failover();
    } else if (std::string_view(args[1]) == "verify" && argc == 3) {
      std::ifstream input(args[2]);
      std::set<std::string> ids;
      std::string id;
      while (std::getline(input, id)) {
        if (id.size() != 36 || ids.size() >= 100000 || !ids.insert(id).second) {
          throw std::runtime_error("invalid reconciliation manifest");
        }
      }
      if (ids.empty() || !input.eof()) {
        throw std::runtime_error("reconciliation manifest unavailable");
      }
      psim::connector_gateway::testing::run(psim::connector_gateway::testing::verify(std::move(ids)));
    } else {
      throw std::runtime_error("unknown gateway probe");
    }
    return 0;
  } catch (const std::exception& error) {
    (void)std::fputs("gateway probe failed: ", stderr);
    (void)std::fputs(error.what(), stderr);
    (void)std::fputs("\n", stderr);
    return 1;
  }
}
