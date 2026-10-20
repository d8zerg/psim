#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <clickhouse/block.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/string.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/audit/v1/audit.pb.h"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Time = google::protobuf::util::TimeUtil;

template <typename T>
T take(p::Result<T> result) {
  if (!result) {
    throw std::runtime_error(std::string(result.error().name()) + ": " + result.error().message());
  }
  return std::move(*result);
}

void check(p::Result<void> result) {
  if (!result) {
    throw std::runtime_error(result.error().message());
  }
}

p::kafka::ClientOptions options(const std::string& identity) {
  p::kafka::ClientOptions value{.bootstrap = identity == "anonymous" ? "kafka-tls:19096" : "kafka-tls:19092",
                                .client_id = "rules-probe",
                                .allow_plaintext = identity == "anonymous"};
  if (identity != "anonymous") {
    value.security = p::kafka::TransportSecurity{
        .tls = {
            .ca_file = ".cache/security-tls/ca.pem",
            .certificate_file = identity == "replay" ? ".cache/rules/replay.pem" : ".cache/security-tls/server.pem",
            .private_key_file = identity == "replay" ? ".cache/rules/replay.key" : ".cache/security-tls/server.key"}};
  }
  return value;
}

template <typename T>
T parse(const Json& json) {
  T message;
  if (!google::protobuf::util::JsonStringToMessage(json.dump(), &message).ok()) {
    throw std::runtime_error("invalid probe fixture");
  }
  return message;
}

asio::awaitable<Json> run(p::async::BlockingPool* blocking, std::string mode, std::string path, std::string identity) {
  const auto executor = co_await asio::this_coro::executor;
  const p::kafka::Dependencies deps{.blocking = blocking};
  const auto client = options(identity);
  const std::vector<std::string> topics{
      "psim.catalog.v1", "psim.config.v1",           "psim.audit.v1",    "psim.events.normalized.v1",
      "psim.signals.v1", "psim.incidents.events.v1", "psim.commands.v1", "psim.response.events.v1"};
  Json report = Json::object();
  if (mode == "seed") {
    std::ifstream file(path);
    Json fixtures;
    file >> fixtures;
    auto producer = take(
        p::kafka::Producer::create(executor, {.client = client, .delivery_timeout = std::chrono::seconds(5)}, deps));
    auto registry = take(p::kafka::SchemaRegistry::create(
        {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
    std::map<std::string, std::int32_t> schemas;
    p::clickhouse::Pool destination(blocking, {.endpoints = {{"clickhouse", 9000}},
                                               .user = "psim",
                                               .password_file = ".cache/processing/clickhouse.password",
                                               .allow_plaintext = true});
    check(co_await destination.execute_ddl(
        "CREATE TABLE IF NOT EXISTS events (tenant_id String,event_id String,payload String,occurred_at "
        "DateTime64(3,'UTC')) ENGINE=ReplacingMergeTree ORDER BY (tenant_id,occurred_at,event_id)"));
    auto tenant = std::make_shared<::clickhouse::ColumnString>();
    auto ids = std::make_shared<::clickhouse::ColumnString>();
    auto payload = std::make_shared<::clickhouse::ColumnString>();
    auto times = std::make_shared<::clickhouse::ColumnDateTime64>(3);
    for (const auto& fixture : fixtures) {
      const auto topic = fixture.at("topic").get<std::string>();
      if (!schemas.contains(topic)) {
        schemas[topic] = take(co_await registry.latest_id(topic + "-value"));
      }
      std::unique_ptr<google::protobuf::Message> message;
      if (topic == "psim.catalog.v1") {
        message = std::make_unique<psim::catalog::v1::CatalogRecord>(
            parse<psim::catalog::v1::CatalogRecord>(fixture.at("value")));
      } else if (topic == "psim.config.v1") {
        message = std::make_unique<psim::config::v1::ConfigRecord>(
            parse<psim::config::v1::ConfigRecord>(fixture.at("value")));
      } else if (topic == "psim.audit.v1") {
        message = std::make_unique<psim::audit::v1::AuditEventRecord>(
            parse<psim::audit::v1::AuditEventRecord>(fixture.at("value")));
      } else if (topic == "psim.events.normalized.v1") {
        auto event = parse<psim::processing::v1::EventRecord>(fixture.at("value"));
        tenant->Append(event.envelope().tenant_id());
        ids->Append(event.envelope().message_id());
        payload->Append(event.SerializeAsString());
        times->Append(Time::TimestampToMilliseconds(event.envelope().occurred_at()));
        message = std::make_unique<psim::processing::v1::EventRecord>(std::move(event));
      } else {
        throw std::runtime_error("unsupported fixture topic");
      }
      std::string bytes;
      p::kafka::wire::write_header(bytes, schemas.at(topic),
                                   p::kafka::wire::message_indexes(*message->GetDescriptor()));
      message->AppendToString(&bytes);
      check(co_await producer->enqueue({.topic = topic, .key = fixture.at("key"), .value = std::move(bytes)}));
    }
    check(co_await producer->flush(std::chrono::seconds(10)));
    ::clickhouse::Block block;
    block.AppendColumn("tenant_id", tenant);
    block.AppendColumn("event_id", ids);
    block.AppendColumn("payload", payload);
    block.AppendColumn("occurred_at", times);
    check(co_await destination.insert("events", block, p::stream::sha256(fixtures.dump())));
    destination.close();
    report["seeded"] = fixtures.size();
  } else if (mode == "deny") {
    for (const auto* topic :
         {"psim.signals.v1", "psim.incidents.events.v1", "psim.commands.v1", "psim.response.events.v1"}) {
      auto producer = take(
          p::kafka::Producer::create(executor, {.client = client, .delivery_timeout = std::chrono::seconds(2)}, deps));
      auto sent = co_await producer->send({.topic = topic, .key = "denial-probe", .value = "must be rejected"});
      if (sent || !sent.error().message().contains("AUTHORIZATION_FAILED")) {
        throw std::runtime_error("expected topic authorization failure: "
                                 + (sent ? std::string("write succeeded") : sent.error().message()));
      }
      report[topic] = sent.error().message();
    }
  } else if (mode == "deny-offsets") {
    auto consumer = take(p::kafka::Consumer::create(executor,
                                                    {.client = client,
                                                     .group_id = "rule-replay-offset-denial",
                                                     .topics = {"psim.events.normalized.v1"},
                                                     .group_protocol = "classic",
                                                     .manual = true},
                                                    deps));
    const p::kafka::Offsets offsets{{{"psim.events.normalized.v1", 0}, 1}};
    check(consumer->assign({{{"psim.events.normalized.v1", 0}, 0}}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (true) {
      auto batch = co_await consumer->poll(1, std::chrono::milliseconds(100));
      if (!batch) {
        throw std::runtime_error(batch.error().message());
      }
      auto committed = co_await consumer->commit(offsets);
      if (committed) {
        throw std::runtime_error("group offset commit succeeded");
      }
      if (committed.error().message().contains("AUTHORIZATION_FAILED")) {
        report["offsetCommitDenied"] = committed.error().message();
        break;
      }
      if (!committed.error().message().contains("_WAIT_COORD") || std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error("expected group authorization failure: " + committed.error().message());
      }
    }
    co_await consumer->close();
  } else if (mode == "observe") {
    auto consumer = take(p::kafka::Consumer::create(
        executor,
        {.client = client, .group_id = "rules-observer", .topics = topics, .group_protocol = "classic", .manual = true},
        deps));
    auto bounds = take(co_await consumer->offset_bounds(std::chrono::seconds(5)));
    check(consumer->assign(bounds.begin));
    report["ends"] = Json::object();
    report["sets"] = Json::array();
    report["audit"] = Json::object();
    for (const auto& [partition, end] : bounds.end) {
      report["ends"][partition.topic][std::to_string(partition.partition)] = end;
    }
    std::map<std::string, std::string> seen;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!std::ranges::all_of(bounds.end, [&](const auto& part) {
      auto current = consumer->positions().find(part.first);
      return current != consumer->positions().end() && current->second >= part.second;
    })) {
      if (std::chrono::steady_clock::now() > deadline) {
        throw std::runtime_error("observer timeout");
      }
      auto batch = take(co_await consumer->poll(1000, std::chrono::milliseconds(50)));
      for (const auto& message : batch) {
        if (message.offset() >= bounds.end.at(message.topic_partition())) {
          continue;
        }
        if (message.topic() == "psim.config.v1") {
          auto value = take(p::kafka::ProtobufSerde<psim::config::v1::ConfigRecord>(1).deserialize(message.value()));
          const auto event_id = value.envelope().message_id();
          const auto digest = p::stream::sha256(message.value());
          if (seen.contains(event_id)) {
            if (seen.at(event_id) != digest) {
              throw std::runtime_error("config identity changed");
            }
            continue;
          }
          seen[event_id] = digest;
          if (value.has_rule_set()) {
            report["sets"].push_back(Json::parse(p::stream::json(value)));
          }
        } else if (message.topic() == "psim.audit.v1") {
          auto value = take(p::kafka::ProtobufSerde<psim::audit::v1::AuditEventRecord>(1).deserialize(message.value()));
          const auto event_id = value.envelope().message_id();
          const auto digest = p::stream::sha256(message.value());
          if (seen.contains(event_id)) {
            if (seen.at(event_id) != digest) {
              throw std::runtime_error("audit identity changed");
            }
            continue;
          }
          seen[event_id] = digest;
          if (value.has_audit_entry()) {
            const auto action = value.audit_entry().action();
            report["audit"][action] = report["audit"].value(action, 0) + 1;
          }
        }
      }
    }
    co_await consumer->close();
  } else {
    throw std::runtime_error("unknown probe operation");
  }
  std::ofstream output(mode == "seed" ? path + ".result.json" : path);
  output << report.dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("cannot write probe report");
  }
  co_return report;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      throw std::runtime_error("usage: psim-rules-probe seed|observe|deny report.json super|replay|anonymous");
    }
    asio::io_context io;
    p::async::BlockingPool blocking(4, 32);
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    auto task = asio::co_spawn(io, run(&blocking, arguments[1], arguments[2], arguments[3]), asio::use_future);
    io.run();
    (void)task.get();
    blocking.stop();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
