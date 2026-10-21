#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <librdkafka/rdkafka.h>
#include <nlohmann/json.hpp>
#include <psim/common/v1/envelope.pb.h>
#include <psim/common/v1/types.pb.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "psim/audit/v1/audit.pb.h"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/stream/replica.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"
#include "psim/processing/v1/internal.pb.h"
#include "psim/processing/v1/signal.pb.h"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Json = nlohmann::json;

template <typename T>
T take(p::Result<T> value) {
  if (!value) {
    throw std::runtime_error(value.error().message());
  }
  return std::move(*value);
}

void check(p::Result<void> value) {
  if (!value) {
    throw std::runtime_error(value.error().message());
  }
}

template <typename T>
std::unique_ptr<google::protobuf::Message> parse(const Json& value) {
  auto result = std::make_unique<T>();
  if (!google::protobuf::util::JsonStringToMessage(value.dump(), result.get()).ok()) {
    throw std::runtime_error("invalid correlation fixture");
  }
  return result;
}

asio::awaitable<void> run(p::async::BlockingPool* blocking, const std::string mode, const std::string path) {
  const auto executor = co_await asio::this_coro::executor;
  const p::kafka::ClientOptions options{
      .bootstrap = "kafka-1:19092",
      .client_id = "correlation-probe",
      .properties = {{"linger.ms", "1"}, {"fetch.wait.max.ms", "1"}, {"socket.nagle.disable", "true"}},
      .allow_plaintext = true};
  const p::kafka::Dependencies deps{.blocking = blocking};
  if (mode == "seed") {
    std::ifstream input(path);
    Json fixtures;
    input >> fixtures;
    auto producer = take(p::kafka::Producer::create(executor, {.client = options}, deps));
    auto registry = take(p::kafka::SchemaRegistry::create(
        {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
    std::map<std::string, std::int32_t> schemas;
    for (const auto& fixture : fixtures) {
      const auto topic = fixture.at("topic").get<std::string>();
      if (!schemas.contains(topic)) {
        schemas[topic] = take(co_await registry.latest_id(topic + "-value"));
      }
      std::unique_ptr<google::protobuf::Message> message;
      if (topic == "psim.config.v1") {
        message = parse<psim::config::v1::ConfigRecord>(fixture.at("value"));
      } else if (topic == "psim.catalog.v1") {
        message = parse<psim::catalog::v1::CatalogRecord>(fixture.at("value"));
      } else if (topic == "psim.events.normalized.v1") {
        message = parse<psim::processing::v1::EventRecord>(fixture.at("value"));
      } else {
        throw std::runtime_error("unsupported fixture topic");
      }
      std::string bytes;
      p::kafka::wire::write_header(bytes, schemas.at(topic),
                                   p::kafka::wire::message_indexes(*message->GetDescriptor()));
      message->AppendToString(&bytes);
      p::kafka::Record record{.topic = topic, .key = fixture.at("key").get<std::string>(), .value = std::move(bytes)};
      if (fixture.contains("partition")) {
        record.partition = fixture.at("partition").get<std::int32_t>();
      }
      check(co_await producer->enqueue(std::move(record)));
    }
    check(co_await producer->flush(std::chrono::seconds(30)));
    co_return;
  }
  if (mode.starts_with("bench-")) {
    std::ifstream file(path);
    Json spec;
    file >> spec;
    const auto count = spec.at("count").get<std::size_t>();
    const auto tenant = spec.at("tenant").get<std::string>();
    const auto site = spec.at("site").get<std::string>();
    const auto seed = spec.at("seed").get<std::string>();
    using Time = google::protobuf::util::TimeUtil;
    if (mode == "bench-replay") {
      auto observer = take(p::kafka::Consumer::create(executor,
                                                      {.client = options,
                                                       .group_id = "correlation-replay-check",
                                                       .topics = {"psim.signals.v1"},
                                                       .group_protocol = "classic",
                                                       .manual = true},
                                                      deps));
      const auto ends = take(co_await observer->end_offsets(std::chrono::seconds(5)));
      Json checkpoint = Json::object();
      for (const auto& [partition, offset] : ends) {
        checkpoint[std::to_string(partition.partition)] = offset;
      }
      std::ofstream(path + ".checkpoint.json") << checkpoint.dump();
      co_await observer->close();
      std::vector<p::kafka::Record> records;
      records.reserve(count);
      auto source = take(co_await p::stream::Replica::open(
          executor, options, deps, {"psim.events.normalized.v1"},
          [&](const p::kafka::Message& message) -> p::Result<void> {
            auto event = p::kafka::ProtobufSerde<psim::processing::v1::EventRecord>(1).deserialize(message.value());
            if (!event) {
              return std::unexpected(event.error());
            }
            if (event->envelope().tenant_id() == tenant && event->envelope().correlation_id() == seed) {
              if (records.size() >= count) {
                return p::fail(p::ErrorCode::kCommonInternal, "unexpected duplicate benchmark input");
              }
              records.push_back({.topic = "psim.events.normalized.v1",
                                 .key = std::string(message.key()),
                                 .value = std::string(message.value())});
            }
            return {};
          }));
      co_await source->close();
      if (records.size() != count) {
        throw std::runtime_error("benchmark input reconciliation failed");
      }
      auto producer = take(p::kafka::Producer::create(executor, {.client = options}, deps));
      for (auto& record : records) {
        check(co_await producer->enqueue(std::move(record)));
      }
      check(co_await producer->flush(std::chrono::seconds(30)));
      co_return;
    }
    if (mode == "bench-verify") {
      std::ifstream file_checkpoint(path + ".checkpoint.json");
      Json checkpoint;
      file_checkpoint >> checkpoint;
      auto source = take(co_await p::stream::Replica::open(
          executor, options, deps, {"psim.signals.v1"}, [&](const p::kafka::Message& message) -> p::Result<void> {
            if (message.offset() < checkpoint.at(std::to_string(message.partition())).get<std::int64_t>()) {
              return {};
            }
            auto signal = p::kafka::ProtobufSerde<psim::processing::v1::SignalRecord>(1).deserialize(message.value());
            if (!signal) {
              return std::unexpected(signal.error());
            }
            if (signal->envelope().tenant_id() == tenant && signal->envelope().correlation_id() == seed) {
              return p::fail(p::ErrorCode::kCommonInternal, "duplicate signal after cold state recovery");
            }
            return {};
          }));
      co_await source->close();
      co_return;
    }
    if (mode == "bench-drained") {
      const std::array inputs{std::pair{"psim.events.normalized.v1", "psim-correlation-router"},
                              std::pair{"psim.correlation.repartition.v1", "psim-correlation-engine"}};
      for (const auto& [topic, group] : inputs) {
        auto source = take(p::kafka::Consumer::create(
            executor,
            {.client = options, .group_id = group, .topics = {topic}, .group_protocol = "classic", .manual = true},
            deps));
        const auto ends = take(co_await source->end_offsets(std::chrono::seconds(5)));
        std::vector<p::kafka::TopicPartition> partitions;
        partitions.reserve(ends.size());
        for (const auto& [partition, end] : ends) {
          partitions.push_back(partition);
        }
        auto offsets = take(co_await source->committed(std::move(partitions), std::chrono::seconds(5)));
        for (const auto& [partition, end] : ends) {
          offsets.try_emplace(partition, 0);
        }
        check(source->assign(offsets));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!std::ranges::all_of(
            ends, [&](const auto& entry) { return source->positions().at(entry.first) >= entry.second; })) {
          if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("pipeline checkpoint inspection timed out");
          }
          auto messages = take(co_await source->poll(512, std::chrono::milliseconds(10)));
          for (const auto& message : messages) {
            if (message.offset() < ends.at(message.topic_partition())) {
              throw std::runtime_error("pipeline still has readable input");
            }
          }
        }
        co_await source->close();
      }
      co_return;
    }
    if (mode == "bench-emit") {
      auto producer = take(p::kafka::Producer::create(executor, {.client = options}, deps));
      const auto metadata_error = take(co_await blocking->run([handle = producer->handle()] {
        std::unique_ptr<const rd_kafka_metadata_t, decltype(&rd_kafka_metadata_destroy)> metadata(
            nullptr, rd_kafka_metadata_destroy);
        return rd_kafka_metadata(handle, 1, nullptr, std::out_ptr(metadata), 10000);
      }));
      if (metadata_error != RD_KAFKA_RESP_ERR_NO_ERROR) {
        throw std::runtime_error("benchmark producer metadata initialization failed");
      }
      auto registry = take(p::kafka::SchemaRegistry::create(
          {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
      const auto schema = take(co_await registry.latest_id("psim.events.normalized.v1-value"));
      std::vector<std::string> zones;
      zones.reserve(1024);
      for (std::size_t index = 0; index < 1024; ++index) {
        zones.push_back(p::stream::uuid5(spec.value("zone_seed", seed), {"zone", std::to_string(index)}));
      }
      const auto rate = spec.at("rate").get<std::int64_t>();
      const auto started = std::chrono::steady_clock::now();
      asio::steady_timer pace(executor);
      for (std::size_t index = 0; index < count; ++index) {
        psim::processing::v1::EventRecord value;
        auto* envelope = value.mutable_envelope();
        const auto id = p::stream::uuid5(seed, {std::to_string(index)});
        envelope->set_message_id(id);
        envelope->set_correlation_id(seed);
        envelope->set_tenant_id(tenant);
        envelope->set_sequence((index / zones.size()) + 1 + spec.value("sequence_offset", std::uint64_t{0}));
        *envelope->mutable_occurred_at() = Time::GetCurrentTime();
        *envelope->mutable_received_at() = envelope->occurred_at();
        *envelope->mutable_produced_at() = envelope->occurred_at();
        envelope->mutable_actor()->set_kind(psim::common::v1::ACTOR_KIND_SERVICE);
        envelope->mutable_actor()->set_id("correlation-benchmark");
        envelope->set_schema_version("1");
        auto* event = value.mutable_event();
        const auto& zone = zones[index % zones.size()];
        event->set_type("intrusion.zone.alarm");
        event->set_site_id(site);
        event->set_zone_id(zone);
        event->set_device_id(zone);
        event->set_source_id(zone);
        event->set_source_epoch(1);
        event->set_severity(psim::common::v1::SEVERITY_HIGH);
        std::string bytes;
        p::kafka::wire::write_header(
            bytes, schema, p::kafka::wire::message_indexes(*psim::processing::v1::EventRecord::GetDescriptor()));
        value.AppendToString(&bytes);
        auto routing_key = site;
        routing_key.push_back(':');
        routing_key += zone;
        check(co_await producer->enqueue(
            {.topic = "psim.events.normalized.v1", .key = std::move(routing_key), .value = std::move(bytes)}));
        if (index % 512 == 511) {
          pace.expires_at(started + std::chrono::microseconds(static_cast<std::int64_t>(index + 1) * 1000000 / rate));
          co_await pace.async_wait(asio::use_awaitable);
        }
      }
      check(co_await producer->flush(std::chrono::seconds(30)));
      co_return;
    }
    auto consumer = take(p::kafka::Consumer::create(executor,
                                                    {.client = options,
                                                     .group_id = "correlation-benchmark",
                                                     .topics = {"psim.signals.v1"},
                                                     .group_protocol = "classic",
                                                     .manual = true},
                                                    deps));
    auto ends = take(co_await consumer->end_offsets(std::chrono::seconds(5)));
    check(consumer->assign(ends));
    std::unordered_set<std::string> expected;
    for (std::size_t index = 0; index < count; ++index) {
      expected.insert(p::stream::uuid5(seed, {std::to_string(index)}));
    }
    std::ofstream(path + ".ready") << "ready\n";
    std::vector<double> latencies;
    std::vector<double> input_to_router;
    std::vector<double> router_to_engine;
    std::vector<double> engine_to_read;
    std::map<std::int64_t, std::vector<double>> latency_seconds;
    double first = 0;
    double last = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (!expected.empty()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error("correlation benchmark timed out with missing signals");
      }
      auto messages = take(co_await consumer->poll(4096, std::chrono::milliseconds(10)));
      const auto observed = static_cast<double>(Time::TimestampToMicroseconds(Time::GetCurrentTime())) / 1000.0;
      for (const auto& message : messages) {
        auto signal = take(p::kafka::ProtobufSerde<psim::processing::v1::SignalRecord>(1).deserialize(message.value()));
        if (signal.envelope().tenant_id() != tenant || signal.envelope().correlation_id() != seed) {
          continue;
        }
        if (signal.signal().event_ids_size() != 1 || expected.erase(signal.signal().event_ids(0)) != 1) {
          throw std::runtime_error("duplicate, damaged or unexpected benchmark signal");
        }
        const auto received =
            static_cast<double>(Time::TimestampToMicroseconds(signal.envelope().received_at())) / 1000.0;
        latencies.push_back(observed - received);
        latency_seconds[static_cast<std::int64_t>(received) / 1000].push_back(observed - received);
        const auto routed = message.header("psim-ts-correlation-router");
        const auto processed = message.header("psim-ts-correlation");
        if (!routed || !processed) {
          throw std::runtime_error("benchmark signal is missing stage timestamps");
        }
        const auto router_ms = static_cast<double>(std::stoll(std::string(*routed))) / 1000000.0;
        const auto engine_ms = static_cast<double>(std::stoll(std::string(*processed))) / 1000000.0;
        input_to_router.push_back(router_ms - received);
        router_to_engine.push_back(engine_ms - router_ms);
        engine_to_read.push_back(observed - engine_ms);
        if (first == 0) {
          first = observed;
        }
        last = observed;
      }
    }
    co_await consumer->close();
    std::ranges::sort(latencies);
    const auto p99 = [](auto& values) {
      std::ranges::sort(values);
      return values[(values.size() - 1) * 99 / 100];
    };
    Json per_second = Json::array();
    for (auto& [second, values] : latency_seconds) {
      per_second.push_back(
          {{"second", second - latency_seconds.begin()->first}, {"events", values.size()}, {"p99_ms", p99(values)}});
    }
    const Json report{{"events", count},
                      {"signals", latencies.size()},
                      {"events_per_second", static_cast<double>(count) * 1000.0 / (last - first)},
                      {"p99_ms", latencies[(latencies.size() - 1) * 99 / 100]},
                      {"p99_input_to_router_ms", p99(input_to_router)},
                      {"p99_router_to_engine_ms", p99(router_to_engine)},
                      {"p99_engine_to_read_ms", p99(engine_to_read)},
                      {"latency_seconds", std::move(per_second)}};
    std::ofstream(path + ".result.json") << report.dump(2) << '\n';
    co_return;
  }
  Json result{{"signals", Json::array()}, {"suppressed", Json::array()}, {"state_partitions", Json::object()},
              {"states", Json::object()}, {"repartitioned", 0},          {"dlq", 0}};
  auto replica = take(co_await p::stream::Replica::open(
      executor, options, deps,
      {"psim.signals.v1", "psim.audit.v1", "psim.correlation.changelog.v1", "psim.correlation.repartition.v1",
       "psim.dlq.correlation.v1", "psim.dlq.correlation-router.v1"},
      [&result](const p::kafka::Message& message) -> p::Result<void> {
        if (message.topic() == "psim.signals.v1") {
          auto value = p::kafka::ProtobufSerde<psim::processing::v1::SignalRecord>(1).deserialize(message.value());
          if (!value) {
            return std::unexpected(value.error());
          }
          result["signals"].push_back(Json::parse(p::stream::json(*value)));
        } else if (message.topic() == "psim.audit.v1") {
          auto value = p::kafka::ProtobufSerde<psim::audit::v1::AuditEventRecord>(1).deserialize(message.value());
          if (!value) {
            return std::unexpected(value.error());
          }
          if (value->audit_entry().action() == "correlation.maintenance_suppressed") {
            result["suppressed"].push_back(Json::parse(p::stream::json(*value)));
          }
        } else if (message.topic() == "psim.correlation.changelog.v1" && message.key() != "__checkpoint") {
          const auto key = std::string(message.key());
          if (message.value().empty()) {
            result["states"].erase(key);
          } else {
            auto value = p::kafka::ProtobufSerde<psim::processing::v1::CorrelationChangelogRecord>(1).deserialize(
                message.value());
            if (!value) {
              return std::unexpected(value.error());
            }
            result["states"][key] = Json::parse(p::stream::json(*value));
            result["state_partitions"][key] = message.partition();
          }
        } else if (message.topic().starts_with("psim.dlq.")) {
          result["dlq"] = result.at("dlq").get<int>() + 1;
        } else if (message.topic() == "psim.correlation.repartition.v1") {
          result["repartitioned"] = result.at("repartitioned").get<int>() + 1;
        }
        return {};
      }));
  co_await replica->close();
  std::ofstream output(path);
  output << result.dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("cannot save correlation observation");
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    return 2;
  }
  try {
    p::async::BlockingPool blocking(4, 32);
    asio::io_context io;
    const std::span args(argv, static_cast<std::size_t>(argc));
    auto result = asio::co_spawn(io, run(&blocking, args[1], args[2]), asio::use_future);
    io.run();
    result.get();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
