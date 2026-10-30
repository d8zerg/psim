#include <grpcpp/support/status_code_enum.h>
#include <psim/connector/v1/administration.pb.h>
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
#include <memory>
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

asio::awaitable<p::kafka::Offsets> capture_load_offsets(p::async::BlockingPool* blocking) {
  auto consumer = p::kafka::Consumer::create(co_await asio::this_coro::executor,
                                             {.client = reader_options(),
                                              .group_id = "load-boundary-" + p::stream::uuid7(),
                                              .topics = {"psim.ingest.raw.v1", "psim.events.normalized.v1"},
                                              .group_protocol = "classic",
                                              .manual = true},
                                             {.blocking = blocking});
  if (!consumer) {
    throw std::runtime_error("load boundary consumer unavailable");
  }
  auto offsets = co_await (*consumer)->end_offsets(std::chrono::seconds(20));
  co_await (*consumer)->close();
  if (!offsets || offsets->empty()) {
    throw std::runtime_error("cannot capture load verification boundaries");
  }
  co_return std::move(*offsets);
}

asio::awaitable<void> verify(std::set<std::string> expected, bool normalized = false, bool check_duplicates = false,
                             p::kafka::Offsets starts = {}) {
  const auto total = expected.size();
  const auto identities = check_duplicates ? expected : std::set<std::string>{};
  auto executor = co_await asio::this_coro::executor;
  auto options = reader_options();
  auto consumer =
      p::kafka::Consumer::create(executor, {.client = std::move(options),
                                            .group_id = "gateway-probe-" + p::stream::uuid7(),
                                            .topics = {normalized ? "psim.events.normalized.v1" : "psim.ingest.raw.v1"},
                                            .group_protocol = "classic",
                                            .manual = !starts.empty()});
  if (!consumer) {
    throw std::runtime_error("probe consumer unavailable");
  }
  if (!starts.empty()) {
    std::erase_if(starts, [normalized](const auto& entry) {
      return entry.first.topic != (normalized ? "psim.events.normalized.v1" : "psim.ingest.raw.v1");
    });
    if (starts.empty() || !(*consumer)->assign(starts)) {
      throw std::runtime_error("cannot assign load verification boundaries");
    }
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
  std::unordered_map<std::string, std::string> incident_sources;
  std::uint64_t outbox_replays = 0;
  std::unordered_map<std::string, std::int64_t> event_times;
  std::unordered_map<std::string, std::int64_t> pending_incidents;
  nlohmann::json incident_origins = nlohmann::json::object();
  auto observations = nlohmann::json::array();
  std::vector<std::uint64_t> signal_histogram(100001);
  std::vector<std::uint64_t> incident_histogram(100001);
  std::map<std::int64_t, std::vector<std::uint64_t>> windows;
  std::map<std::int64_t, std::uint64_t> window_counts;
  std::map<std::int64_t, std::vector<std::uint64_t>> signal_windows;
  std::map<std::int64_t, std::uint64_t> signal_window_counts;
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
        observations.push_back({{"kind", "signal"},
                                {"signal_id", identity},
                                {"occurred_ms", occurred},
                                {"produced_ms", milliseconds(value->envelope().produced_at())},
                                {"observed_ms", now}});
        count(&signal_histogram, now - occurred);
        auto& signal_window = signal_windows[now / 10000];
        if (signal_window.empty()) {
          signal_window.resize(100001);
        }
        count(&signal_window, now - occurred);
        ++signal_window_counts[now / 10000];
        if (auto pending = pending_incidents.find(identity); pending != pending_incidents.end()) {
          incident(pending->second - occurred, pending->second);
          incident_origins[incident_sources.at(identity)] = occurred;
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
        incident_sources[identity] = value->envelope().correlation_id();
        observations.push_back({{"kind", "incident"},
                                {"signal_id", identity},
                                {"message_id", value->envelope().message_id()},
                                {"correlation_id", value->envelope().correlation_id()},
                                {"produced_ms", milliseconds(value->envelope().produced_at())},
                                {"observed_ms", now}});
        if (auto found = event_times.find(identity); found != event_times.end()) {
          incident(now - found->second, now);
          incident_origins[value->envelope().correlation_id()] = found->second;
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
  std::size_t maximum_signal_window_p99 = 0;
  for (const auto& [window, histogram] : signal_windows) {
    maximum_signal_window_p99 =
        std::max(maximum_signal_window_p99, percentile(histogram, signal_window_counts.at(window)));
  }
  output << nlohmann::json{{"signals", signals.size()},
                           {"incidents", incident_count},
                           {"duplicates", duplicates},
                           {"outbox_replays", outbox_replays},
                           {"maximum_signal_gap_ms", maximum_gap},
                           {"signal_p99_ms", percentile(signal_histogram, signals.size())},
                           {"maximum_window_signal_p99_ms", maximum_signal_window_p99},
                           {"incident_p99_ms", percentile(incident_histogram, incident_count)},
                           {"maximum_window_incident_p99_ms", maximum_window_p99},
                           {"windows", intervals},
                           {"incident_origins_ms", incident_origins},
                           {"incident_message_ids", incidents},
                           {"observations", observations},
                           {"pending", event_times.size() + pending_incidents.size()}}
                .dump()
         << '\n';
}

struct RollingResult {
  nlohmann::json report;
  std::set<std::string> expected;
};

RollingResult rolling_client(int index, int clients, p::connector::Client* client,
                             std::chrono::steady_clock::time_point begin, std::atomic_bool* cancelled,
                             std::atomic_int* measured) {
  const auto& configuration = probe_configuration();
  const auto rate = configuration.value("rolling_rate", 100);
  const auto load_devices = configuration.value("load_devices", rate >= 50000);
  if (rate < 1 || rate > 75000) {
    throw std::runtime_error("invalid rolling publication rate");
  }
  const auto output_prefix = std::filesystem::path(fixture_directory()) / ("rolling-client-" + std::to_string(index));
  std::this_thread::sleep_until(begin);
  const auto limit = begin + std::chrono::seconds(configuration.value("rolling_seconds", 1200));
  const auto stop = std::filesystem::path(fixture_directory()) / "rolling.stop";
  const auto connector = identity(19 + index);
  std::set<std::string> expected;
  std::string last;
  std::ofstream ledger(output_prefix.string() + "-ledger.txt");
  const auto total_rate = rate >= 5000 ? rate + (rate / 50) : rate;
  const auto scheduled_rate = (total_rate / clients) + (index < total_rate % clients ? 1 : 0);
  const auto batch_size = std::clamp(scheduled_rate / 10, 1, 1000);
  const auto alarm_stride = static_cast<std::size_t>(scheduled_rate);
  std::uint64_t alarms = 0;
  double publication_seconds = 0;
  double max_publication_ms = 0;
  std::uint64_t background = 0;
  nlohmann::json intervals = nlohmann::json::array();
  std::uint64_t previous_acknowledged = 0;
  double previous_seconds = 0;
  std::array<std::uint64_t, 10> device_counters{};
  nlohmann::json source_ranges = nlohmann::json::object();
  // Spread independent publishers across one batch period inside the shared measurement window.
  const auto publication_begin =
      begin
      + std::chrono::microseconds((static_cast<std::int64_t>(index) * batch_size * 1000000) / scheduled_rate / clients);
  std::this_thread::sleep_until(publication_begin);
  const auto first_event_ms = milliseconds(Util::GetCurrentTime());
  auto progress_at = begin + std::chrono::seconds(1);
  while (!cancelled->load() && !std::filesystem::exists(stop) && std::chrono::steady_clock::now() < limit) {
    std::vector<wire::ConnectorEvent> inputs;
    for (int item = 0; item < batch_size; ++item) {
      wire::ConnectorEvent input;
      input.set_source_epoch(configuration.value("rolling_epoch", std::uint32_t{1}));
      const auto alarm = index == 0 && (expected.size() + static_cast<std::size_t>(item)) % alarm_stride == 0;
      input.set_source_ref("door/alarm");
      if (!alarm && load_devices) {
        // Spread site traffic through the cycle and offset independent clients.
        const auto slot = ((background * 37) + static_cast<std::uint64_t>(index)) % 100;
        const auto site = slot < 30 ? 0 : 1 + (((slot - 30) * 9) / 70);
        const auto device = ((device_counters.at(site)++ % static_cast<std::uint64_t>(1000 / clients))
                             * static_cast<std::uint64_t>(clients))
                            + static_cast<std::uint64_t>(index);
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
    const auto publication_started = std::chrono::steady_clock::now();
    auto published = client->publish_batch(std::move(inputs));
    const auto publication_elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - publication_started).count();
    publication_seconds += publication_elapsed;
    max_publication_ms = std::max(max_publication_ms, publication_elapsed * 1000);
    if (!published) {
      throw std::runtime_error(published.error().message() + ": " + client->diagnostics().last_error);
    }
    for (const auto& event : *published) {
      auto& range = source_ranges[event.source_ref()];
      if (range.is_null()) {
        range = {{"first", event.source_seq()},
                 {"last", event.source_seq()},
                 {"count", 0},
                 {"epoch", event.source_epoch()},
                 {"connector", connector}};
      } else if (event.source_seq() != range.at("last").get<std::uint64_t>() + 1
                 || event.source_epoch() != range.at("epoch")) {
        throw std::runtime_error("load source sequence is not contiguous");
      }
      range["last"] = event.source_seq();
      range["count"] = range.at("count").get<std::uint64_t>() + 1;
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
        publication_begin
        + std::chrono::microseconds(static_cast<std::int64_t>(expected.size()) * 1000000 / scheduled_rate);
    std::this_thread::sleep_until(std::min(next, limit));
    if (std::chrono::steady_clock::now() >= progress_at) {
      const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
      const auto diagnostics = client->diagnostics();
      intervals.push_back({{"seconds", elapsed},
                           {"duration_seconds", elapsed - previous_seconds},
                           {"acknowledged", diagnostics.acknowledged - previous_acknowledged},
                           {"pending", diagnostics.pending_events}});
      previous_acknowledged = diagnostics.acknowledged;
      previous_seconds = elapsed;
      const auto path = std::filesystem::path(output_prefix.string() + "-progress.json");
      std::ofstream progress(path.string() + ".tmp");
      progress << nlohmann::json{{"published", expected.size()},
                                 {"acknowledged", diagnostics.acknowledged},
                                 {"seconds", elapsed},
                                 {"pending", diagnostics.pending_events},
                                 {"buffer_bytes", diagnostics.buffer_bytes},
                                 {"connected", diagnostics.connected},
                                 {"sessions", diagnostics.sessions},
                                 {"last_error", diagnostics.last_error},
                                 {"batches_sent", diagnostics.batches_sent},
                                 {"batch_acks", diagnostics.batch_acks},
                                 {"max_acknowledgement_ms", diagnostics.max_acknowledgement_ms},
                                 {"max_buffer_acknowledgement_ms", diagnostics.max_buffer_acknowledgement_ms},
                                 {"max_pending_read_ms", diagnostics.max_pending_read_ms},
                                 {"publication_seconds", publication_seconds},
                                 {"max_publication_ms", max_publication_ms},
                                 {"published_per_second", static_cast<double>(expected.size()) / elapsed},
                                 {"acknowledged_per_second", static_cast<double>(diagnostics.acknowledged) / elapsed}}
                      .dump();
      progress.close();
      std::filesystem::rename(path.string() + ".tmp", path);
      progress_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }
  }
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
  const auto before_drain = client->diagnostics();
  const auto last_event_ms = milliseconds(Util::GetCurrentTime());
  measured->fetch_add(1);
  if (seconds > previous_seconds) {
    intervals.push_back({{"seconds", seconds},
                         {"duration_seconds", seconds - previous_seconds},
                         {"acknowledged", before_drain.acknowledged - previous_acknowledged},
                         {"pending", before_drain.pending_events}});
  }
  ledger.close();
  const auto drained = client->flush(std::chrono::seconds(90));
  const auto diagnostics = client->diagnostics();
  client->stop();
  auto result = nlohmann::json{{"client", index},
                               {"connector", connector},
                               {"target_rate", (rate / clients) + (index < rate % clients ? 1 : 0)},
                               {"pending_before_drain", before_drain.pending_events},
                               {"published", expected.size()},
                               {"acknowledged", diagnostics.acknowledged},
                               {"signals_expected", alarms},
                               {"virtual_devices", load_devices ? 10000 / clients : 1},
                               {"sites", load_devices ? 10 : 1},
                               {"site_background_events", device_counters},
                               {"background_events_per_second", static_cast<double>(background) / seconds},
                               {"seconds", seconds},
                               {"events_per_second", static_cast<double>(expected.size()) / seconds},
                               {"acknowledged_before_drain", before_drain.acknowledged},
                               {"acknowledged_per_second", static_cast<double>(before_drain.acknowledged) / seconds},
                               {"publication_batch_events", batch_size},
                               {"publication_seconds", publication_seconds},
                               {"max_publication_ms", max_publication_ms},
                               {"intervals", intervals},
                               {"source_ranges", source_ranges},
                               {"first_event_ms", first_event_ms},
                               {"last_event_ms", last_event_ms},
                               {"drained", drained},
                               {"connected_before_drain", before_drain.connected},
                               {"sessions", diagnostics.sessions},
                               {"last_error", diagnostics.last_error},
                               {"rejected", diagnostics.rejected},
                               {"pending", diagnostics.pending_events}};
  std::ofstream(output_prefix.string() + "-result.json") << result.dump() << '\n';
  if (!drained) {
    throw std::runtime_error("rolling SDK failed to drain: pending=" + std::to_string(diagnostics.pending_events)
                             + ", sessions=" + std::to_string(diagnostics.sessions) + ", " + diagnostics.last_error);
  }
  if (diagnostics.acknowledged != expected.size() || diagnostics.rejected != 0 || diagnostics.pending_events != 0) {
    throw std::runtime_error("rolling SDK acknowledgement invariants failed");
  }
  if (index == 0) {
    std::ofstream(std::filesystem::path(fixture_directory()) / "event.json")
        << nlohmann::json{{"event_id", last}}.dump() << '\n';
  }
  return {.report = std::move(result), .expected = std::move(expected)};
}

void rolling() {
  const auto& configuration = probe_configuration();
  const auto count = configuration.value("rolling_clients", 1);
  const auto rate = configuration.value("rolling_rate", 100);
  if (count < 1 || count > 100 || 1000 % count != 0 || rate < count || rate > 75000
      || (count > 1 && !configuration.value("load_devices", false))) {
    throw std::runtime_error("invalid rolling client count or rate");
  }
  std::vector<std::unique_ptr<p::connector::Client>> clients;
  for (int index = 0; index < count; ++index) {
    auto client = std::make_unique<p::connector::Client>(p::connector::Options{
        .endpoint = configuration.value("sdk_endpoint", "connector-gateway:7443"),
        .ca_file = tls_directory() + "/ca.pem",
        .certificate_file = prefix(19 + index) + ".pem",
        .private_key_file = prefix(19 + index) + ".key",
        .buffer_directory = (std::filesystem::path(fixture_directory()) / ("sdk-" + p::stream::uuid7())).string(),
        .max_batch_events = 1000,
        .max_batch_bytes = std::size_t{256} * 1024});
    if (auto started = client->start(); !started) {
      throw std::runtime_error(started.error().message());
    }
    clients.push_back(std::move(client));
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  while (!std::ranges::all_of(clients, [](const auto& client) { return client->diagnostics().connected; })) {
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error("rolling publishers did not establish every gateway session");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  std::vector<RollingResult> results(static_cast<std::size_t>(count));
  p::async::BlockingPool blocking(1, 4);
  const auto starts = run(capture_load_offsets(&blocking));
  std::vector<std::string> errors(static_cast<std::size_t>(count));
  std::atomic_bool cancelled{false};
  std::atomic_int measured{0};
  const auto begin = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  const auto limit = begin + std::chrono::seconds(configuration.value("rolling_seconds", 1200));
  std::vector<std::jthread> workers;
  workers.reserve(clients.size());
  for (std::size_t index = 0; index < clients.size(); ++index) {
    workers.emplace_back([&, index] {
      try {
        results[index] =
            rolling_client(static_cast<int>(index), count, clients[index].get(), begin, &cancelled, &measured);
      } catch (const std::exception& error) {
        errors[index] = error.what();
        cancelled = true;
      }
    });
  }
  std::println("READY");
  if (std::fflush(stdout) != 0) {
    cancelled = true;
    throw std::runtime_error("cannot report rolling publisher readiness");
  }
  nlohmann::json intervals = nlohmann::json::array();
  std::uint64_t previous_acknowledged = 0;
  double previous_seconds = 0;
  auto next_sample = begin + std::chrono::seconds(1);
  while (measured.load() != count && !cancelled.load()) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_sample && now < limit && measured.load() == 0) {
      std::uint64_t acknowledged = 0;
      std::size_t pending = 0;
      auto rows = nlohmann::json::array();
      for (std::size_t index = 0; index < clients.size(); ++index) {
        const auto diagnostics = clients[index]->diagnostics();
        acknowledged += diagnostics.acknowledged;
        pending += diagnostics.pending_events;
        rows.push_back({{"client", index},
                        {"acknowledged", diagnostics.acknowledged},
                        {"pending", diagnostics.pending_events},
                        {"connected", diagnostics.connected},
                        {"last_error", diagnostics.last_error},
                        {"max_acknowledgement_ms", diagnostics.max_acknowledgement_ms}});
      }
      const auto sampled = std::chrono::steady_clock::now();
      if (sampled < limit && measured.load() == 0) {
        const auto seconds = std::chrono::duration<double>(sampled - begin).count();
        intervals.push_back({{"seconds", seconds},
                             {"duration_seconds", seconds - previous_seconds},
                             {"acknowledged", acknowledged - previous_acknowledged},
                             {"pending", pending}});
        previous_acknowledged = acknowledged;
        previous_seconds = seconds;
        const auto path = std::filesystem::path(fixture_directory()) / "rolling-progress.json";
        std::ofstream(path.string() + ".tmp") << nlohmann::json{
            {"seconds", seconds},
            {"acknowledged", acknowledged},
            {"pending", pending},
            {"acknowledged_per_second", static_cast<double>(acknowledged) / seconds},
            {"clients", rows}}.dump() << '\n';
        std::filesystem::rename(path.string() + ".tmp", path);
      }
      next_sample = sampled + std::chrono::seconds(1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  std::println("MEASURED");
  (void)std::fflush(stdout);
  workers.clear();
  for (std::size_t index = 0; index < errors.size(); ++index) {
    if (!errors[index].empty()) {
      throw std::runtime_error("rolling client " + std::to_string(index) + ": " + errors[index]);
    }
  }
  auto report = results.front().report;
  for (const auto* field : {"client", "connector", "target_rate", "publication_batch_events", "drained",
                            "connected_before_drain", "sessions", "last_error"}) {
    report.erase(field);
  }
  report["virtual_devices"] = configuration.value("load_devices", rate >= 50000) ? 10000 : 1;
  report["publication_seconds"] = 0.0;
  report["max_publication_ms"] = 0.0;
  report["pending_before_drain"] = 0;
  report["clients"] = nlohmann::json::array();
  report["source_ranges"] = nlohmann::json::object();
  report["site_background_events"] = std::array<std::uint64_t, 10>{};
  for (const auto* field :
       {"published", "acknowledged", "acknowledged_before_drain", "pending", "rejected", "signals_expected"}) {
    report[field] = 0;
  }
  double seconds = 0;
  std::set<std::string> expected;
  for (auto& result : results) {
    auto& row = result.report;
    report["publication_seconds"] =
        report.at("publication_seconds").get<double>() + row.at("publication_seconds").get<double>();
    report["max_publication_ms"] =
        std::max(report.at("max_publication_ms").get<double>(), row.at("max_publication_ms").get<double>());
    report["pending_before_drain"] =
        report.at("pending_before_drain").get<std::uint64_t>() + row.at("pending_before_drain").get<std::uint64_t>();
    seconds = std::max(seconds, row.at("seconds").get<double>());
    for (const auto* field :
         {"published", "acknowledged", "acknowledged_before_drain", "pending", "rejected", "signals_expected"}) {
      report[field] = report.at(field).get<std::uint64_t>() + row.at(field).get<std::uint64_t>();
    }
    for (std::size_t site = 0; site < 10; ++site) {
      report["site_background_events"][site] = report["site_background_events"][site].get<std::uint64_t>()
                                               + row["site_background_events"][site].get<std::uint64_t>();
    }
    for (const auto& [reference, range] : row.at("source_ranges").items()) {
      if (report["source_ranges"].contains(reference)) {
        throw std::runtime_error("rolling clients share a source reference");
      }
      report["source_ranges"][reference] = range;
    }
    report["first_event_ms"] =
        std::min(report.at("first_event_ms").get<std::int64_t>(), row.at("first_event_ms").get<std::int64_t>());
    report["last_event_ms"] =
        std::max(report.at("last_event_ms").get<std::int64_t>(), row.at("last_event_ms").get<std::int64_t>());
    auto summary = row;
    summary.erase("source_ranges");
    report["clients"].push_back(std::move(summary));
    expected.merge(result.expected);
    if (!result.expected.empty()) {
      throw std::runtime_error("rolling clients share an event identity");
    }
  }
  const auto acknowledged = report.at("acknowledged_before_drain").get<std::uint64_t>();
  intervals.push_back({{"seconds", seconds},
                       {"duration_seconds", seconds - previous_seconds},
                       {"acknowledged", acknowledged - previous_acknowledged}});
  report["seconds"] = seconds;
  report["client_count"] = count;
  report["kafka_start_offsets"] = nlohmann::json::array();
  for (const auto& [partition, offset] : starts) {
    report["kafka_start_offsets"].push_back(
        {{"topic", partition.topic}, {"partition", partition.partition}, {"offset", offset}});
  }
  report["intervals"] = intervals;
  report["events_per_second"] = report.at("published").get<double>() / seconds;
  report["acknowledged_per_second"] = static_cast<double>(acknowledged) / seconds;
  report["background_events_per_second"] =
      (report.at("published").get<double>() - report.at("signals_expected").get<double>()) / seconds;
  std::ofstream(std::filesystem::path(fixture_directory()) / "rolling-result.json") << report.dump() << '\n';
  std::println("rolling SDK: clients={}, published={}, acknowledged={}", count, expected.size(),
               report.at("acknowledged").get<std::uint64_t>());
  auto normalized = expected;
  run(verify(std::move(expected), false, true, starts));
  run(verify(std::move(normalized), true, true, starts));
}

void enroll_load() {
  const auto count = probe_configuration().value("rolling_clients", 100);
  if (count < 1 || count > 100 || 1000 % count != 0) {
    throw std::runtime_error("invalid rolling client count");
  }
  for (int index = 19; index < 19 + count; ++index) {
    wire::ManageRequest request;
    request.set_request_id(p::stream::uuid7());
    request.set_action(wire::REGISTRATION_ACTION_INSPECT);
    request.mutable_connector()->set_connector_id(identity(index));
    wire::ManageResponse response;
    const auto status = manage(request, &response);
    if (!status.ok()) {
      if (status.error_code() != grpc::StatusCode::PERMISSION_DENIED) {
        throw std::runtime_error("cannot inspect load connector: " + status.error_message());
      }
      (void)enroll(index, 1000000, std::chrono::seconds(20));
    }
  }
  std::println("Independent load connectors enrolled: {}", count);
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
    } else if (std::string_view(args[1]) == "enroll-load") {
      psim::connector_gateway::testing::enroll_load();
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
