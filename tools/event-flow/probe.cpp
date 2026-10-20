#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <psim/common/v1/envelope.pb.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "psim/incident/v1/events.pb.h"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"
#include "psim/processing/v1/signal.pb.h"

namespace {
namespace p = psim::platform;
using Json = nlohmann::json;

boost::asio::awaitable<int> probe(Json inputs) {
  p::async::BlockingPool blocking(2, 8);
  auto executor = co_await boost::asio::this_coro::executor;
  auto consumer = p::kafka::Consumer::create(
      executor,
      {.client = {.bootstrap = "kafka-1:19092", .client_id = "event-flow-probe", .allow_plaintext = true},
       .group_id = "probe-" + p::stream::uuid7(),
       .topics = {"psim.ingest.raw.v1", "psim.events.normalized.v1", "psim.signals.v1", "psim.incidents.events.v1"}},
      {.blocking = &blocking});
  if (!consumer) {
    throw std::runtime_error("probe consumer failed");
  }
  std::map<std::string, std::int64_t> expected;
  for (const auto& event : inputs) {
    expected[event.at("event_id").get<std::string>()] = event.at("occurred_ns").get<std::int64_t>();
  }
  std::map<std::string, std::size_t> counts;
  Json latencies = Json::object();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < deadline
         && (counts["raw"] < expected.size() * 2 || counts["normalized"] < expected.size()
             || counts["signal"] < expected.size() || counts["incident"] < expected.size())) {
    auto batch = co_await (*consumer)->poll(128, std::chrono::milliseconds(100));
    if (!batch) {
      throw std::runtime_error("probe poll failed");
    }
    for (const auto& message : *batch) {
      psim::common::v1::Envelope envelope;
      std::string stage;
      if (message.topic() == "psim.ingest.raw.v1") {
        auto record = p::kafka::ProtobufSerde<psim::ingest::v1::RawEventRecord>(1).deserialize(message.value());
        if (!record) {
          continue;
        }
        envelope = record->envelope();
        stage = "raw";
      }
      if (message.topic() == "psim.events.normalized.v1") {
        auto record = p::kafka::ProtobufSerde<psim::processing::v1::EventRecord>(1).deserialize(message.value());
        if (!record) {
          continue;
        }
        envelope = record->envelope();
        stage = "normalized";
      }
      if (message.topic() == "psim.signals.v1") {
        auto record = p::kafka::ProtobufSerde<psim::processing::v1::SignalRecord>(1).deserialize(message.value());
        if (!record) {
          continue;
        }
        envelope = record->envelope();
        stage = "signal";
      }
      if (message.topic() == "psim.incidents.events.v1") {
        auto record = p::kafka::ProtobufSerde<psim::incident::v1::IncidentEventRecord>(1).deserialize(message.value());
        if (!record) {
          continue;
        }
        envelope = record->envelope();
        stage = "incident";
      }
      if (!expected.contains(envelope.correlation_id())) {
        continue;
      }
      ++counts[stage];
      if (!message.header("traceparent")) {
        throw std::runtime_error("missing causal trace");
      }
      if (stage == "signal" && !latencies.contains(envelope.correlation_id())) {
        const auto produced = google::protobuf::util::TimeUtil::TimestampToNanoseconds(envelope.produced_at());
        latencies[envelope.correlation_id()] =
            static_cast<double>(produced - expected.at(envelope.correlation_id())) / 1000000.0;
      }
      if (stage == "incident") {
        for (const auto* name : {"psim-ts-ingest", "psim-ts-normalizer", "psim-ts-correlation", "psim-ts-incident"}) {
          if (!message.header(name)) {
            throw std::runtime_error("missing stage timestamp");
          }
        }
      }
    }
  }
  co_await (*consumer)->close();
  if (counts["raw"] != expected.size() * 2 || counts["normalized"] != expected.size()
      || counts["signal"] != expected.size() || counts["incident"] != expected.size()) {
    throw std::runtime_error("pipeline accounting mismatch: " + Json(counts).dump());
  }
  std::cout << Json{{"counts", counts}, {"signal_ms", latencies}}.dump() << '\n';
  co_return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      return 2;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): one required command-line argument
    std::ifstream file(argv[1]);
    Json input;
    file >> input;
    boost::asio::io_context io;
    auto result = boost::asio::co_spawn(io, probe(std::move(input)), boost::asio::use_future);
    io.run();
    return result.get();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
