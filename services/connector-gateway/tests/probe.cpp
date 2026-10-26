#include <psim/connector/v1/connector.pb.h>
#include <psim/incident/v1/events.pb.h>
#include <psim/ingest/v1/raw_event.pb.h>
#include <psim/processing/v1/event.pb.h>
#include <psim/processing/v1/signal.pb.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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

asio::awaitable<void> verify(std::set<std::string> expected, bool normalized = false, bool check_duplicates = false) {
  const auto total = expected.size();
  const auto identities = check_duplicates ? expected : std::set<std::string>{};
  auto executor = co_await asio::this_coro::executor;
  auto options = reader_options();
  auto consumer =
      p::kafka::Consumer::create(executor, {.client = std::move(options),
                                            .group_id = "gateway-probe-" + p::stream::uuid7(),
                                            .topics = {normalized ? "psim.events.normalized.v1" : "psim.ingest.raw.v1"},
                                            .group_protocol = "classic"});
  if (!consumer) {
    throw std::runtime_error("probe consumer unavailable");
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60 + static_cast<std::int64_t>(total / 25000));
  std::size_t records = 0;
  std::size_t duplicates = 0;
  std::optional<std::chrono::steady_clock::time_point> tail;
  while (std::chrono::steady_clock::now() < deadline
         && (!expected.empty() || (tail && std::chrono::steady_clock::now() < *tail))) {
    auto batch = co_await (*consumer)->poll(10000, std::chrono::milliseconds(100));
    if (!batch) {
      throw std::runtime_error("probe consumer failed");
    }
    for (const auto& item : *batch) {
      std::string identity;
      if (normalized) {
        auto value = p::kafka::ProtobufSerde<processing::v1::EventRecord>(1).deserialize(item.value());
        if (value) {
          identity = value->envelope().message_id();
        }
      } else {
        auto value = p::kafka::ProtobufSerde<ingest::v1::RawEventRecord>(1).deserialize(item.value());
        if (value) {
          identity = value->envelope().message_id();
        }
      }
      if (expected.erase(identity) != 0) {
        ++records;
      } else if (check_duplicates && identities.contains(identity)) {
        ++duplicates;
      }
    }
    if (expected.empty() && check_duplicates && !tail) {
      tail = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
  }
  co_await (*consumer)->close();
  std::println("gateway reconciliation: normalized={}, expected={}, found={}, missing={}, transport_duplicates={}",
               normalized, total, records, expected.size(), duplicates);
  if (!expected.empty() || (normalized && duplicates != 0)) {
    throw std::runtime_error("gateway reconciliation found missing or duplicate events");
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

std::int64_t milliseconds(const google::protobuf::Timestamp& value) {
  return (value.seconds() * 1000) + (value.nanos() / 1000000);
}

std::size_t percentile(const std::vector<std::uint64_t>& histogram, std::uint64_t total) {
  const auto target = ((total * 99) + 99) / 100;
  std::uint64_t sum = 0;
  for (std::size_t index = 0; index < histogram.size(); ++index) {
    sum += histogram[index];
    if (sum >= target && total != 0) {
      return index;
    }
  }
  return histogram.size();
}

asio::awaitable<void> watch_rolling() {
  auto executor = co_await asio::this_coro::executor;
  auto consumer = p::kafka::Consumer::create(executor, {.client = reader_options(),
                                                        .group_id = "upgrade-watch-" + p::stream::uuid7(),
                                                        .topics = {"psim.signals.v1", "psim.incidents.events.v1"},
                                                        .group_protocol = "classic",
                                                        .auto_offset_reset = "latest"});
  if (!consumer) {
    throw std::runtime_error("upgrade observation consumer unavailable");
  }
  const auto& configuration = probe_configuration();
  const auto site = configuration.at("rolling_site").get<std::string>();
  const auto stop = std::filesystem::path(fixture_directory()) / "rolling-watch.stop";
  std::unordered_set<std::string> signals;
  std::unordered_map<std::string, std::string> incidents;
  std::uint64_t outbox_replays = 0;
  std::unordered_map<std::string, std::int64_t> event_times;
  std::unordered_map<std::string, std::int64_t> pending_incidents;
  std::vector<std::uint64_t> signal_histogram(100001);
  std::vector<std::uint64_t> incident_histogram(100001);
  std::map<std::int64_t, std::vector<std::uint64_t>> windows;
  std::map<std::int64_t, std::uint64_t> window_counts;
  std::uint64_t duplicates = 0;
  std::uint64_t incident_count = 0;
  std::int64_t maximum_gap = 0;
  std::optional<std::chrono::steady_clock::time_point> previous;
  bool ready = false;
  auto count = [](std::vector<std::uint64_t>* histogram, std::int64_t delay) {
    ++histogram->at(static_cast<std::size_t>(std::clamp<std::int64_t>(delay, 0, 100000)));
  };
  auto incident = [&](std::int64_t delay, std::int64_t observed) {
    count(&incident_histogram, delay);
    auto& window = windows[observed / 10000];
    if (window.empty()) {
      window.resize(100001);
    }
    count(&window, delay);
    ++window_counts[observed / 10000];
    ++incident_count;
  };
  while (!std::filesystem::exists(stop)) {
    auto batch = co_await (*consumer)->poll(1000, std::chrono::milliseconds(100));
    if (!batch) {
      throw std::runtime_error("upgrade observation poll failed");
    }
    if (!ready && !(*consumer)->assignment().empty()) {
      std::println("READY");
      if (std::fflush(stdout) != 0) {
        throw std::runtime_error("cannot report observation readiness");
      }
      ready = true;
    }
    for (const auto& item : *batch) {
      const auto now =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();
      if (item.topic() == "psim.signals.v1") {
        auto value = p::kafka::ProtobufSerde<processing::v1::SignalRecord>(1).deserialize(item.value());
        if (!value || !value->has_signal() || value->signal().site_id() != site) {
          continue;
        }
        const auto identity = value->envelope().message_id();
        if (!signals.insert(identity).second) {
          ++duplicates;
          continue;
        }
        const auto point = std::chrono::steady_clock::now();
        if (previous) {
          maximum_gap =
              std::max(maximum_gap, std::chrono::duration_cast<std::chrono::milliseconds>(point - *previous).count());
        }
        previous = point;
        const auto occurred = milliseconds(value->signal().last_event_at());
        count(&signal_histogram, now - occurred);
        if (auto pending = pending_incidents.find(identity); pending != pending_incidents.end()) {
          incident(pending->second - occurred, pending->second);
          pending_incidents.erase(pending);
        } else {
          event_times.emplace(identity, occurred);
        }
        if (signals.size() == 1) {
          std::println("FLOW");
          if (std::fflush(stdout) != 0) {
            throw std::runtime_error("cannot report observation flow");
          }
        }
      } else {
        auto value = p::kafka::ProtobufSerde<incident::v1::IncidentEventRecord>(1).deserialize(item.value());
        if (!value) {
          continue;
        }
        std::string identity;
        if (value->has_incident_created() && value->incident_created().incident().site_id() == site) {
          identity = value->incident_created().signal_id();
        } else if (value->has_signal_attached() && value->signal_attached().incident().site_id() == site) {
          identity = value->signal_attached().signal_id();
        }
        if (identity.empty()) {
          continue;
        }
        const auto [existing, inserted] = incidents.emplace(identity, value->envelope().message_id());
        if (!inserted) {
          if (existing->second == value->envelope().message_id()) {
            ++outbox_replays;
          } else {
            ++duplicates;
          }
          continue;
        }
        if (auto found = event_times.find(identity); found != event_times.end()) {
          incident(now - found->second, now);
          event_times.erase(found);
        } else {
          pending_incidents.emplace(identity, now);
        }
      }
    }
  }
  co_await (*consumer)->close();
  std::size_t maximum_window_p99 = 0;
  auto intervals = nlohmann::json::array();
  for (const auto& [window, histogram] : windows) {
    const auto p99 = percentile(histogram, window_counts.at(window));
    maximum_window_p99 = std::max(maximum_window_p99, p99);
    intervals.push_back({{"window", window}, {"incidents", window_counts.at(window)}, {"p99_ms", p99}});
  }
  std::ofstream output(std::filesystem::path(fixture_directory()) / "rolling-watch-result.json");
  output << nlohmann::json{{"signals", signals.size()},
                           {"incidents", incident_count},
                           {"duplicates", duplicates},
                           {"outbox_replays", outbox_replays},
                           {"maximum_signal_gap_ms", maximum_gap},
                           {"signal_p99_ms", percentile(signal_histogram, signals.size())},
                           {"incident_p99_ms", percentile(incident_histogram, incident_count)},
                           {"maximum_window_incident_p99_ms", maximum_window_p99},
                           {"windows", intervals},
                           {"pending", event_times.size() + pending_incidents.size()}}
                .dump()
         << '\n';
}

void rolling() {
  const auto& configuration = probe_configuration();
  const auto rate = configuration.value("rolling_rate", 100);
  if (rate < 1 || rate > 50000) {
    throw std::runtime_error("invalid rolling publication rate");
  }
  const auto directory = std::filesystem::path(fixture_directory()) / ("sdk-" + p::stream::uuid7());
  p::connector::Client client({.endpoint = configuration.value("sdk_endpoint", "connector-gateway:7443"),
                               .ca_file = tls_directory() + "/ca.pem",
                               .certificate_file = prefix(19) + ".pem",
                               .private_key_file = prefix(19) + ".key",
                               .buffer_directory = directory.string(),
                               .max_batch_events = 1000,
                               .max_batch_bytes = std::size_t{256} * 1024});
  auto started = client.start();
  if (!started) {
    throw std::runtime_error(started.error().message());
  }
  const auto connection_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (!client.diagnostics().connected) {
    if (std::chrono::steady_clock::now() >= connection_deadline) {
      throw std::runtime_error("rolling publisher did not establish a gateway session");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const auto begin = std::chrono::steady_clock::now();
  const auto limit = begin + std::chrono::seconds(configuration.value("rolling_seconds", 1200));
  const auto stop = std::filesystem::path(fixture_directory()) / "rolling.stop";
  std::set<std::string> expected;
  std::string last;
  std::ofstream ledger(std::filesystem::path(fixture_directory()) / "rolling-ledger.txt");
  const auto scheduled_rate = rate >= 50000 ? 51000 : rate;
  const auto batch_size = std::clamp(scheduled_rate / 100, 1, 1000);
  const auto alarm_stride = static_cast<std::size_t>(scheduled_rate);
  std::uint64_t alarms = 0;
  std::uint64_t background = 0;
  std::array<std::uint64_t, 10> device_counters{};
  auto progress_at = begin + std::chrono::seconds(1);
  std::println("READY");
  if (std::fflush(stdout) != 0) {
    throw std::runtime_error("cannot report rolling publisher readiness");
  }
  while (!std::filesystem::exists(stop) && std::chrono::steady_clock::now() < limit) {
    std::vector<wire::ConnectorEvent> inputs;
    for (int index = 0; index < batch_size; ++index) {
      wire::ConnectorEvent input;
      const auto alarm = (expected.size() + static_cast<std::size_t>(index)) % alarm_stride == 0;
      input.set_source_ref("door/alarm");
      if (!alarm && rate >= 50000) {
        const auto slot = background % 100;
        const auto site = slot < 30 ? 0 : 1 + (((slot - 30) * 9) / 70);
        const auto device = device_counters.at(site)++ % 1000;
        if (site != 0 || device != 0) {
          input.set_source_ref("load/" + std::to_string(site) + "/" + std::to_string(device));
        }
      }
      if (!alarm) {
        ++background;
      }
      input.set_raw_code(alarm ? "ALARM" : "BACKGROUND");
      *input.mutable_occurred_at() = Util::GetCurrentTime();
      inputs.push_back(std::move(input));
    }
    auto published = client.publish_batch(std::move(inputs));
    if (!published) {
      throw std::runtime_error(published.error().message());
    }
    for (const auto& event : *published) {
      expected.insert(event.event_id());
      ledger << event.event_id() << '\n';
      if (event.raw_code() == "ALARM") {
        last = event.event_id();
        ++alarms;
      }
    }
    ledger.flush();
    if (!ledger) {
      throw std::runtime_error("cannot persist the rolling publication ledger");
    }
    const auto next =
        begin + std::chrono::microseconds(static_cast<std::int64_t>(expected.size()) * 1000000 / scheduled_rate);
    std::this_thread::sleep_until(next);
    if (std::chrono::steady_clock::now() >= progress_at) {
      const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
      const auto diagnostics = client.diagnostics();
      const auto path = std::filesystem::path(fixture_directory()) / "rolling-progress.json";
      std::ofstream progress(path.string() + ".tmp");
      progress << nlohmann::json{{"published", expected.size()},
                                 {"acknowledged", diagnostics.acknowledged},
                                 {"seconds", elapsed},
                                 {"pending", diagnostics.pending_events},
                                 {"published_per_second", static_cast<double>(expected.size()) / elapsed},
                                 {"acknowledged_per_second", static_cast<double>(diagnostics.acknowledged) / elapsed}}
                      .dump();
      progress.close();
      std::filesystem::rename(path.string() + ".tmp", path);
      progress_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }
  }
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
  ledger.close();
  if (!client.flush(std::chrono::seconds(90))) {
    throw std::runtime_error("rolling SDK failed to drain");
  }
  const auto diagnostics = client.diagnostics();
  client.stop();
  if (diagnostics.acknowledged != expected.size() || diagnostics.rejected != 0 || diagnostics.pending_events != 0) {
    throw std::runtime_error("rolling SDK acknowledgement invariants failed");
  }
  std::ofstream report(std::filesystem::path(fixture_directory()) / "rolling-result.json");
  report << nlohmann::json{{"published", expected.size()},
                           {"acknowledged", diagnostics.acknowledged},
                           {"signals_expected", alarms},
                           {"virtual_devices", rate >= 50000 ? 10000 : 1},
                           {"sites", rate >= 50000 ? 10 : 1},
                           {"site_background_events", device_counters},
                           {"background_events_per_second", static_cast<double>(background) / seconds},
                           {"seconds", seconds},
                           {"events_per_second", static_cast<double>(expected.size()) / seconds},
                           {"rejected", diagnostics.rejected},
                           {"pending", diagnostics.pending_events}}
                .dump()
         << '\n';
  report.close();
  std::ofstream event(std::filesystem::path(fixture_directory()) / "event.json");
  event << nlohmann::json{{"event_id", last}}.dump() << '\n';
  std::println("rolling SDK: published={}, acknowledged={}, sessions={}, rejected={}, pending={}", expected.size(),
               diagnostics.acknowledged, diagnostics.sessions, diagnostics.rejected, diagnostics.pending_events);
  auto normalized = expected;
  run(verify(std::move(expected), false, true));
  run(verify(std::move(normalized), true, true));
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
    } else if (std::string_view(args[1]) == "watch-rolling") {
      psim::connector_gateway::testing::run(psim::connector_gateway::testing::watch_rolling());
    } else if (std::string_view(args[1]) == "rolling") {
      psim::connector_gateway::testing::rolling();
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
