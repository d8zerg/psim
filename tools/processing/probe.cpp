#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "psim/catalog/v1/catalog.pb.h"
#include "psim/common/v1/envelope.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Time = google::protobuf::util::TimeUtil;
constexpr const char* kTenant = "11111111-1111-4111-8111-111111111111";
constexpr const char* kConnector = "22222222-2222-4222-8222-222222222222";
constexpr const char* kSite = "33333333-3333-4333-8333-333333333333";
constexpr const char* kZone = "44444444-4444-4444-8444-444444444444";
constexpr const char* kDevice = "55555555-5555-4555-8555-555555555555";
constexpr const char* kType = "77777777-7777-4777-8777-777777777777";

std::string source(const std::string& ref) {
  return p::stream::uuid5("728d47ed-1c69-5809-a2df-6e8b98e48ca6", {kConnector, ref});
}

void check(p::Result<void> value) {
  if (!value) {
    throw std::runtime_error(value.error().message());
  }
}

template <typename T>
T take(p::Result<T> value) {
  if (!value) {
    throw std::runtime_error(value.error().message());
  }
  return std::move(*value);
}

void envelope(psim::common::v1::Envelope* value) {
  value->set_tenant_id(kTenant);
  value->set_message_id(p::stream::uuid7());
  value->set_sequence(1);
  *value->mutable_occurred_at() = Time::GetCurrentTime();
  *value->mutable_produced_at() = value->occurred_at();
  value->set_correlation_id(value->message_id());
  value->mutable_actor()->set_kind(psim::common::v1::ACTOR_KIND_SERVICE);
  value->mutable_actor()->set_id("processing-probe");
}

asio::awaitable<int> run(std::string mode, std::size_t count, std::string seed) {
  p::async::BlockingPool blocking(4, 32);
  const auto executor = co_await asio::this_coro::executor;
  const p::kafka::ClientOptions options{
      .bootstrap = "kafka-1:19092",
      .client_id = "processing-probe",
      .properties = {{"linger.ms", "1"}, {"fetch.wait.max.ms", "1"}, {"socket.nagle.disable", "true"}},
      .allow_plaintext = true};
  const p::kafka::Dependencies deps{.blocking = &blocking};
  auto producer = take(p::kafka::Producer::create(executor, {.client = options}, deps));
  auto registry = take(p::kafka::SchemaRegistry::create(
      {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
  std::map<std::string, std::int32_t> schemas;
  for (const auto* topic : {"psim.catalog.v1", "psim.config.v1", "psim.ingest.raw.v1"}) {
    schemas[topic] = take(co_await registry.latest_id(std::string(topic) + "-value"));
  }
  auto record = [&schemas](const std::string& topic, std::string key, const google::protobuf::Message& value) {
    std::string bytes;
    p::kafka::wire::write_header(bytes, schemas.at(topic), p::kafka::wire::message_indexes(*value.GetDescriptor()));
    value.AppendToString(&bytes);
    return p::kafka::Record{.topic = topic, .key = std::move(key), .value = std::move(bytes)};
  };
  if (mode == "seed") {
    psim::catalog::v1::CatalogRecord value;
    envelope(value.mutable_envelope());
    auto* connector = value.mutable_connector();
    connector->set_connector_id(kConnector);
    connector->set_connector_type("simulator");
    connector->set_state(psim::catalog::v1::RESOURCE_STATE_ACTIVE);
    connector->set_version(1);
    check(co_await producer->enqueue(record("psim.catalog.v1", "connector:" + std::string(kConnector), value)));
    value.clear_connector();
    auto* type = value.mutable_device_type();
    type->set_device_type_id(kType);
    type->set_state(psim::catalog::v1::RESOURCE_STATE_ACTIVE);
    type->set_version(1);
    check(co_await producer->enqueue(record("psim.catalog.v1", "device_type:" + std::string(kType), value)));
    value.clear_device_type();
    auto* zone = value.mutable_location();
    zone->set_location_id(kZone);
    zone->set_site_id(kSite);
    zone->set_kind(psim::catalog::v1::LOCATION_KIND_ZONE);
    zone->set_state(psim::catalog::v1::RESOURCE_STATE_ACTIVE);
    zone->set_version(1);
    check(co_await producer->enqueue(record("psim.catalog.v1", "location:" + std::string(kZone), value)));
    for (std::size_t index = 0; index < count; ++index) {
      value.clear_location();
      value.clear_device();
      const auto ref = "door-" + std::to_string(index + 1);
      const auto zone_id = index == 0 ? std::string(kZone) : p::stream::uuid5(kZone, {ref});
      auto* location = value.mutable_location();
      location->set_location_id(zone_id);
      location->set_site_id(kSite);
      location->set_kind(psim::catalog::v1::LOCATION_KIND_ZONE);
      location->set_state(psim::catalog::v1::RESOURCE_STATE_ACTIVE);
      location->set_version(2);
      check(co_await producer->enqueue(record("psim.catalog.v1", "location:" + zone_id, value)));
      value.clear_location();
      auto* device = value.mutable_device();
      device->set_device_id(index == 0 ? std::string(kDevice) : p::stream::uuid5(kDevice, {ref}));
      device->set_connector_id(kConnector);
      device->set_device_type_id(kType);
      device->set_site_id(kSite);
      device->set_zone_id(zone_id);
      device->set_state(psim::catalog::v1::RESOURCE_STATE_ACTIVE);
      device->set_version(2);
      auto* channel = device->add_sources();
      channel->set_external_ref(ref);
      channel->set_source_id(source(ref));
      check(co_await producer->enqueue(record("psim.catalog.v1", "device:" + device->device_id(), value)));
    }
    psim::config::v1::ConfigRecord config;
    envelope(config.mutable_envelope());
    auto* mapping = config.mutable_mapping_rule_set();
    mapping->set_connector_type("simulator");
    mapping->set_taxonomy_version("1");
    mapping->set_version(2);
    mapping->set_mapping_rule_set_id(kType);
    for (const auto& [code, type_name] : std::map<std::string, std::string>{{"BENCHMARK", "generic.alarm"},
                                                                            {"ALARM", "intrusion.zone.alarm"},
                                                                            {"RESTORE", "intrusion.zone.restore"},
                                                                            {"FIRE", "fire.detector.alarm"},
                                                                            {"OFFLINE", "technical.device.offline"}}) {
      auto* rule = mapping->add_rules();
      rule->set_raw_code(code);
      rule->set_type(type_name);
    }
    check(
        co_await producer->enqueue(record("psim.config.v1", "mapping/" + std::string(kTenant) + "/simulator", config)));
    check(co_await producer->flush(std::chrono::seconds(30)));
    co_return 0;
  }
  if (!p::stream::uuid(seed)) {
    throw std::runtime_error("seed must be a UUID");
  }
  if (mode == "emit" || mode == "burst" || mode == "large" || mode == "badkey" || mode == "invalid") {
    const auto started = std::chrono::steady_clock::now();
    asio::steady_timer pace(executor);
    for (std::size_t index = 0; index < count; ++index) {
      if (mode != "burst" && mode != "large" && index % 100 == 0) {
        pace.expires_at(started + std::chrono::microseconds(index * 20));
        co_await pace.async_wait(asio::use_awaitable);
      }
      psim::ingest::v1::RawEventRecord value;
      envelope(value.mutable_envelope());
      auto* env = value.mutable_envelope();
      env->set_message_id(p::stream::uuid5(seed, {std::to_string(index)}));
      env->set_correlation_id(seed);
      env->set_sequence((mode == "large" ? index : index / 100) + 1);
      *env->mutable_received_at() = env->occurred_at();
      auto* raw = value.mutable_raw_event();
      const auto ref = "door-" + std::to_string(mode == "large" ? 1 : (index % 100) + 1);
      raw->set_source_ref(ref);
      raw->set_source_id(source(ref));
      raw->set_connector_id(kConnector);
      raw->set_source_epoch(static_cast<std::uint32_t>(std::stoul(seed.substr(0, 8), nullptr, 16)));
      raw->set_raw_code(mode == "invalid" ? "UNMAPPED" : "BENCHMARK");
      if (mode == "large") {
        (*raw->mutable_attributes())["binary"] = std::string(65536, '\1');
      }
      raw->set_gateway_session_id(seed);
      raw->set_observation_session_id(seed);
      raw->set_live_observation(true);
      check(co_await producer->enqueue(
          record("psim.ingest.raw.v1", mode == "badkey" ? "wrong/" + raw->source_id() : raw->source_id(), value)));
    }
    check(co_await producer->flush(std::chrono::seconds(30)));
    std::cout << nlohmann::json{{"emitted", count},
                                {"seconds",
                                 std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()}}
                     .dump()
              << '\n';
    co_return 0;
  }
  if (mode != "collect" && mode != "observe" && mode != "dlq") {
    throw std::runtime_error("unknown mode");
  }
  const auto* topic = mode == "dlq" ? "psim.dlq.normalizer.v1" : "psim.events.normalized.v1";
  auto consumer = take(p::kafka::Consumer::create(executor,
                                                  {.client = options,
                                                   .group_id = "processing-check-" + p::stream::uuid7(),
                                                   .topics = {topic},
                                                   .auto_offset_reset = mode == "observe" ? "latest" : "earliest"},
                                                  deps));
  std::map<std::string, std::string> zones;
  for (std::size_t index = 0; index < 100; ++index) {
    const auto ref = "door-" + std::to_string(index + 1);
    zones[source(ref)] = index == 0 ? std::string(kZone) : p::stream::uuid5(kZone, {ref});
  }
  std::unordered_set<std::string> ids;
  std::vector<double> latency;
  std::vector<double> visible;
  std::vector<double> stages;
  bool ready = false;
  std::int64_t first_received_ns = 0;
  std::int64_t last_visible_ns = 0;
  std::size_t duplicates = 0;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  auto complete = deadline;
  while (std::chrono::steady_clock::now() < deadline && std::chrono::steady_clock::now() < complete) {
    auto messages = take(co_await consumer->poll(4096, std::chrono::milliseconds(5)));
    const bool positioned = std::ranges::all_of(consumer->assignment(), [&consumer](const auto& partition) {
      return consumer->positions().contains(partition);
    });
    if (!ready && !consumer->assignment().empty() && (mode != "observe" || positioned)) {
      ready = true;
      std::cout << "ready\n" << std::flush;
    }
    for (const auto& message : messages) {
      psim::common::v1::Envelope env;
      if (mode == "dlq") {
        auto value = p::kafka::ProtobufSerde<psim::ingest::v1::RawEventRecord>(1).deserialize(message.value());
        if (!value) {
          continue;
        }
        env = value->envelope();
      } else {
        auto value = p::kafka::ProtobufSerde<psim::processing::v1::EventRecord>(1).deserialize(message.value());
        if (!value) {
          continue;
        }
        env = value->envelope();
        if (env.correlation_id() == seed) {
          if (value->event().site_id() != kSite || value->event().zone_id() != zones.at(value->event().source_id())
              || value->event().device_type_id() != kType) {
            throw std::runtime_error("catalog enrichment mismatch");
          }
          const auto received_ns = Time::TimestampToNanoseconds(env.received_at());
          const auto visible_ns = Time::TimestampToNanoseconds(Time::GetCurrentTime());
          first_received_ns = first_received_ns == 0 ? received_ns : std::min(first_received_ns, received_ns);
          last_visible_ns = std::max(last_visible_ns, visible_ns);
          const auto start = message.header("psim-ts-normalizer-start");
          if (!start) {
            throw std::runtime_error("normalization start timestamp is missing");
          }
          stages.push_back(static_cast<double>(visible_ns - std::stoll(std::string(*start))) / 1000000.0);
          visible.push_back(static_cast<double>(Time::TimestampToNanoseconds(Time::GetCurrentTime())
                                                - Time::TimestampToNanoseconds(env.received_at()))
                            / 1000000.0);
          latency.push_back(static_cast<double>(Time::TimestampToNanoseconds(env.produced_at())
                                                - Time::TimestampToNanoseconds(env.received_at()))
                            / 1000000.0);
        }
      }
      if (env.correlation_id() != seed) {
        continue;
      }
      if (!ids.insert(env.message_id()).second) {
        ++duplicates;
      }
    }
    if (ids.size() >= count && complete == deadline) {
      complete = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
  }
  co_await consumer->close();
  if (ids.size() != count || duplicates != 0) {
    throw std::runtime_error("event reconciliation failed: " + std::to_string(ids.size()) + "/" + std::to_string(count)
                             + ", duplicates " + std::to_string(duplicates));
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (!ids.contains(p::stream::uuid5(seed, {std::to_string(index)}))) {
      throw std::runtime_error("event identifier mismatch");
    }
  }
  std::ranges::sort(latency);
  std::ranges::sort(visible);
  std::ranges::sort(stages);
  std::cout << nlohmann::json{{"visible_seconds", static_cast<double>(last_visible_ns - first_received_ns) / 1e9},
                              {"unique", ids.size()},
                              {"duplicates", duplicates},
                              {"p99_stage_ms", stages.empty() ? 0 : stages.at((stages.size() - 1) * 99 / 100)},
                              {"p99_visible_ms", visible.empty() ? 0 : visible.at((visible.size() - 1) * 99 / 100)},
                              {"p99_handler_end_ms", latency.empty() ? 0 : latency.at((latency.size() - 1) * 99 / 100)}}
                   .dump()
            << '\n';
  co_return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3 || argc > 4) {
      return 2;
    }
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): bounded CLI arguments
    const std::string mode = argv[1];
    const auto count = std::stoull(argv[2]);
    const std::string seed = argc == 4 ? argv[3] : "";
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (count == 0 || count > 1000000) {
      return 2;
    }
    asio::io_context io;
    auto result = asio::co_spawn(io, run(mode, count, seed), asio::use_future);
    io.run();
    return result.get();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
