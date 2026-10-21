#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <psim/processing/v1/signal.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "psim/common/v1/envelope.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/response/v1/events.pb.h"
#include "psim/response/v1/run.pb.h"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Time = google::protobuf::util::TimeUtil;

template <typename T>
T take(p::Result<T> result) {
  if (!result) {
    throw std::runtime_error(result.error().message());
  }
  return std::move(*result);
}

asio::awaitable<int> emit(Json input) {
  p::async::BlockingPool blocking(4, 32);
  auto executor = co_await asio::this_coro::executor;
  auto producer = take(p::kafka::Producer::create(
      executor, {.client = {.bootstrap = "kafka-1:19092", .client_id = "incident-probe", .allow_plaintext = true}},
      {.blocking = &blocking}));
  auto registry = take(p::kafka::SchemaRegistry::create(
      {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
  if (input.value("mode", "") == "response") {
    const auto schema = take(co_await registry.latest_id("psim.response.events.v1-value"));
    psim::response::v1::ResponseEventRecord record;
    auto* origin = record.mutable_envelope();
    origin->set_tenant_id(input.at("tenant"));
    origin->set_message_id(p::stream::uuid7());
    origin->set_sequence(input.value("runVersion", std::uint64_t{1}));
    *origin->mutable_occurred_at() = Time::GetCurrentTime();
    *origin->mutable_produced_at() = origin->occurred_at();
    origin->set_correlation_id(origin->message_id());
    origin->set_schema_version("1");
    origin->mutable_actor()->set_kind(psim::common::v1::ACTOR_KIND_SERVICE);
    origin->mutable_actor()->set_id("response-probe");
    if (input.contains("level")) {
      auto* event = record.mutable_escalation_level_triggered();
      event->set_incident_id(input.at("incident"));
      event->set_level(input.at("level").get<std::uint32_t>());
    } else {
      auto* event = record.mutable_run_updated();
      event->set_change(psim::response::v1::RUN_CHANGE_MANDATORY_STEPS_CHANGED);
      auto* run = event->mutable_run();
      run->set_run_id(input.at("run"));
      run->set_incident_id(input.at("incident"));
      run->set_state(psim::response::v1::RUN_STATE_RUNNING);
      run->set_version(input.value("runVersion", std::uint64_t{1}));
      for (const auto& step : input.value("blocking", Json::array())) {
        run->add_blocking_step_ids(step.get<std::string>());
      }
    }
    std::string bytes;
    p::kafka::wire::write_header(
        bytes, schema, p::kafka::wire::message_indexes(*psim::response::v1::ResponseEventRecord::descriptor()));
    record.AppendToString(&bytes);
    for (int index = 0; index < input.value("repeat", 1); ++index) {
      auto sent = co_await producer->enqueue(
          {.topic = "psim.response.events.v1", .key = input.at("incident").get<std::string>(), .value = bytes});
      if (!sent) {
        throw std::runtime_error("response enqueue failed");
      }
    }
    auto flushed = co_await producer->flush(std::chrono::seconds(30));
    if (!flushed) {
      throw std::runtime_error("response flush failed");
    }
    std::cout << Json::array({origin->message_id()}).dump() << '\n';
    producer.reset();
    co_return 0;
  }
  const auto schema = take(co_await registry.latest_id("psim.signals.v1-value"));
  const auto count = input.value("count", 1);
  const auto repeat = input.value("repeat", 1);
  Json output = Json::array();
  for (int index = 0; index < count; ++index) {
    psim::processing::v1::SignalRecord record;
    auto* envelope = record.mutable_envelope();
    envelope->set_message_id(p::stream::uuid5("1191e62e-a3ab-4645-9b40-b1f998307f7c",
                                              {input.at("seed").get<std::string>(), std::to_string(index)}));
    envelope->set_tenant_id(input.at("tenant"));
    envelope->set_sequence(1);
    envelope->set_correlation_id(envelope->message_id());
    envelope->set_schema_version("1");
    *envelope->mutable_occurred_at() = Time::GetCurrentTime();
    *envelope->mutable_produced_at() = envelope->occurred_at();
    envelope->mutable_actor()->set_kind(psim::common::v1::ACTOR_KIND_SERVICE);
    envelope->mutable_actor()->set_id("incident-probe");
    auto* signal = record.mutable_signal();
    signal->mutable_origin()->set_kind(psim::processing::v1::SIGNAL_ORIGIN_KIND_EXTERNAL);
    signal->mutable_origin()->set_producer("incident-probe");
    signal->set_correlation_key(input.at("seed"));
    signal->set_grouping_key(input.value("group", input.at("seed").get<std::string>()));
    if (input.value("uniqueGroups", false)) {
      signal->set_grouping_key(signal->grouping_key() + ":" + std::to_string(index));
    }
    signal->set_site_id(input.at("site"));
    signal->set_zone_id(input.value("zone", ""));
    signal->set_incident_type_id(input.at("type"));
    signal->set_priority(static_cast<psim::common::v1::Priority>(input.value("priority", 2)));
    signal->set_summary("Incident acceptance signal");
    signal->add_event_ids(p::stream::uuid7());
    *signal->mutable_first_event_at() = Time::NanosecondsToTimestamp(
        Time::TimestampToNanoseconds(envelope->occurred_at()) + input.value("offsetNs", std::int64_t{0}));
    *signal->mutable_last_event_at() = signal->first_event_at();
    std::string bytes;
    p::kafka::wire::write_header(bytes, schema,
                                 p::kafka::wire::message_indexes(*psim::processing::v1::SignalRecord::descriptor()));
    record.AppendToString(&bytes);
    for (int delivery = 0; delivery < repeat; ++delivery) {
      auto sent =
          co_await producer->enqueue({.topic = "psim.signals.v1", .key = signal->grouping_key(), .value = bytes});
      if (!sent) {
        throw std::runtime_error("signal enqueue failed");
      }
    }
    output.push_back(envelope->message_id());
  }
  auto flushed = co_await producer->flush(std::chrono::seconds(30));
  if (!flushed) {
    throw std::runtime_error("signal flush failed");
  }
  std::cout << output.dump() << '\n';
  producer.reset();
  co_return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::invalid_argument("manifest file required");
    }
    const std::span args(argv, static_cast<std::size_t>(argc));
    std::ifstream file(args[1]);
    Json input = Json::parse(file);
    asio::io_context io;
    auto result = asio::co_spawn(io, emit(std::move(input)), asio::use_future);
    io.run();
    return result.get();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
