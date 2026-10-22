#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <psim/audit/v1/audit.pb.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/config/v1/config.pb.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <print>
#include <ratio>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Clock = std::chrono::system_clock;
using Util = google::protobuf::util::TimeUtil;

asio::awaitable<void> observe(p::async::BlockingPool* blocking, std::string path, const bool* stopping) {
  const auto executor = co_await asio::this_coro::executor;
  auto created = p::kafka::Consumer::create(
      executor,
      {.client = {.bootstrap = "kafka-1:19092", .client_id = "catalog-observer", .allow_plaintext = true},
       .group_id = "catalog-observer-" + p::stream::uuid7(),
       .topics = {"psim.catalog.v1", "psim.config.v1", "psim.audit.v1"},
       .auto_offset_reset = "latest"},
      {.blocking = blocking});
  if (!created) {
    throw std::runtime_error("observer consumer failed");
  }
  auto consumer = std::move(*created);
  Json report{{"ready", false},
              {"deviceCount", 0},
              {"maxDelayMs", 0.0},
              {"audit", Json::object()},
              {"maintenance", Json::object()},
              {"catalogVersions", Json::object()}};
  std::map<std::string, std::pair<std::uint64_t, std::string>> versions;
  std::set<std::string> devices;
  auto written = Clock::now();
  while (!*stopping) {
    auto batch = co_await consumer->poll(2048, std::chrono::milliseconds(50));
    if (!batch) {
      throw std::runtime_error("observer polling failed");
    }
    std::set<std::string> assigned;
    for (const auto& partition : consumer->assignment()) {
      assigned.insert(partition.topic);
    }
    report["ready"] = assigned.size() == 3;
    for (const auto& message : *batch) {
      psim::common::v1::Envelope envelope;
      Json body;
      if (message.topic() == "psim.catalog.v1") {
        auto record = p::kafka::ProtobufSerde<psim::catalog::v1::CatalogRecord>(1).deserialize(message.value());
        if (!record) {
          throw std::runtime_error("invalid catalog protobuf");
        }
        envelope = record->envelope();
        const auto fingerprint = p::stream::sha256(message.value().substr(5));
        const auto previous = versions.find(std::string(message.key()));
        if (previous != versions.end() && previous->second.first > envelope.sequence()) {
          throw std::runtime_error("catalog version moved backwards");
        }
        // Broker retries preserve the same record; replay from outbox also preserves its identity.
        if (previous != versions.end() && previous->second.first == envelope.sequence()
            && previous->second.second != fingerprint) {
          throw std::runtime_error("catalog version changed its contents");
        }
        versions[std::string(message.key())] = {envelope.sequence(), fingerprint};
        if (record->has_device()) {
          devices.insert(record->device().device_id());
        } else {
          report["catalogVersions"][std::string(message.key())] = envelope.sequence();
        }
      } else if (message.topic() == "psim.config.v1") {
        auto record = p::kafka::ProtobufSerde<psim::config::v1::ConfigRecord>(1).deserialize(message.value());
        if (!record) {
          throw std::runtime_error("invalid config protobuf");
        }
        envelope = record->envelope();
        if (record->has_maintenance_window()) {
          const auto& window = record->maintenance_window();
          body = Json::parse(p::stream::json(window));
          body["observedMs"] =
              std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
          report["maintenance"][window.maintenance_id()].push_back(std::move(body));
        }
      } else {
        auto record = p::kafka::ProtobufSerde<psim::audit::v1::AuditEventRecord>(1).deserialize(message.value());
        if (!record) {
          throw std::runtime_error("invalid audit protobuf");
        }
        envelope = record->envelope();
        if (record->has_audit_entry()) {
          const auto action = record->audit_entry().action();
          const auto& details = record->audit_entry().details();
          if (action.starts_with("catalog.") && !action.starts_with("catalog.import.")) {
            if (!details.contains("snapshot") || !details.contains("version")
                || record->envelope().causation_id().empty()) {
              throw std::runtime_error("catalog audit lacks its version snapshot");
            }
            const auto snapshot = Json::parse(details.at("snapshot"));
            const auto version = snapshot.at("version").is_string()
                                     ? snapshot.at("version").get<std::string>()
                                     : std::to_string(snapshot.at("version").get<std::uint64_t>());
            if (version != details.at("version")) {
              throw std::runtime_error("audit version does not match its snapshot");
            }
            report["auditSnapshots"] = report.value("auditSnapshots", 0) + 1;
          }
          report["audit"][action] = report["audit"].value(action, 0) + 1;
        }
      }
      if (envelope.has_occurred_at()) {
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count()
                             - (static_cast<double>(Util::TimestampToNanoseconds(envelope.occurred_at())) / 1000000.0);
        report["maxDelayMs"] = std::max(report["maxDelayMs"].get<double>(), elapsed);
      }
    }
    if (Clock::now() - written >= std::chrono::milliseconds(250)) {
      std::string ids;
      for (const auto& id : devices) {
        ids += id + '\n';
      }
      report["deviceCount"] = devices.size();
      report["deviceDigest"] = p::stream::sha256(ids);
      report["writtenMs"] =
          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
      std::ofstream output(path + ".tmp");
      output << report.dump(2);
      output.close();
      if (!output) {
        throw std::runtime_error("cannot write observer report");
      }
      std::filesystem::rename(path + ".tmp", path);
      written = Clock::now();
    }
  }
  co_await consumer->close();
}
}  // namespace

int main(int argc, char** argv) {  // NOLINT(bugprone-exception-escape): all standard exceptions are reported below.
  try {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() != 2) {
      return 2;
    }
    asio::io_context io;
    p::async::BlockingPool blocking(2, 16);
    bool stopping = false;
    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) { stopping = true; });
    auto result = asio::co_spawn(io, observe(&blocking, arguments[1], &stopping), asio::use_future);
    io.run();
    result.get();
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "catalog observer: {}", error.what());
    return 1;
  }
}
