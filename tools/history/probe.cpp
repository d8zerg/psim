#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
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
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "psim/common/v1/types.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/incident/v1/events.pb.h"
#include "psim/incident/v1/incident.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"
#include "psim/processing/v1/signal.pb.h"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Time = google::protobuf::util::TimeUtil;

template <class T>
T take(p::Result<T> r) {
  if (!r) {
    throw std::runtime_error(r.error().message());
  }
  return std::move(*r);
}

void take(p::Result<void> result) {
  if (!result) {
    throw std::runtime_error(result.error().message());
  }
}

std::string identity(const Json& input, const std::string& kind, std::size_t index) {
  return p::stream::uuid5(input.at("seed").get<std::string>(), {kind, std::to_string(index)});
}

void envelope(psim::common::v1::Envelope* e, const Json& input, const std::string& id, std::int64_t when) {
  e->set_message_id(id);
  e->set_tenant_id(input.at("tenant"));
  e->set_sequence(1);
  e->set_correlation_id(input.at("seed"));
  e->set_schema_version("1");
  *e->mutable_occurred_at() = Time::MillisecondsToTimestamp(when);
  *e->mutable_produced_at() = e->occurred_at();
  *e->mutable_received_at() = e->occurred_at();
  e->mutable_actor()->set_kind(psim::common::v1::ACTOR_KIND_SERVICE);
  e->mutable_actor()->set_id("history-probe");
}

asio::awaitable<int> emit(Json input) {
  p::async::BlockingPool blocking(4, 64);
  auto executor = co_await asio::this_coro::executor;
  auto producer = take(p::kafka::Producer::create(executor,
                                                  {
                                                      .client = {.bootstrap = "kafka-1:19092",
                                                                 .client_id = "history-probe",
                                                                 .properties = {{"linger.ms", "1"}},
                                                                 .allow_plaintext = true},
                                                  },
                                                  {.blocking = &blocking}));
  auto registry = take(p::kafka::SchemaRegistry::create(
      {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
  const auto mode = input.at("mode").get<std::string>();
  std::string topic = "psim.config.v1";
  if (mode == "events") {
    topic = "psim.events.normalized.v1";
  }
  if (mode == "signals") {
    topic = "psim.signals.v1";
  }
  if (mode == "incidents") {
    topic = "psim.incidents.events.v1";
  }
  const auto schema = take(co_await registry.latest_id(std::string(topic) + "-value"));
  const auto count = input.value("count", std::size_t{1});
  Json ids = Json::array();
  asio::steady_timer pace(executor);
  const auto started = std::chrono::steady_clock::now();
  const auto base = input.at("timeMs").get<std::int64_t>();
  for (std::size_t i = 0; i < count; ++i) {
    if (input.value("rate", 0) > 0 && i % 100 == 0) {
      pace.expires_at(
          started
          + std::chrono::microseconds(static_cast<std::int64_t>(i) * 1000000 / input.at("rate").get<std::int64_t>()));
      co_await pace.async_wait(asio::use_awaitable);
    }
    std::string bytes;
    std::string id;
    std::string key;
    std::unique_ptr<google::protobuf::Message> record;
    if (mode == "events") {
      auto event = std::make_unique<psim::processing::v1::EventRecord>();
      id = identity(input, "event", i);
      const auto when = base
                        + (input.value("spreadDay", false)
                               ? (static_cast<std::int64_t>(i) * 86400000 / static_cast<std::int64_t>(count)) - 86400000
                               : static_cast<std::int64_t>(i));
      envelope(event->mutable_envelope(), input, id, when);
      auto* e = event->mutable_event();
      e->set_site_id(input.at("site"));
      e->set_zone_id(input.at("zone"));
      const auto zones = input.value("zones", std::size_t{1});
      if (zones > 1) {
        e->set_zone_id(p::stream::uuid5(input.at("zone").get<std::string>(), {std::to_string(i % zones)}));
      }
      e->set_device_id(input.at("device"));
      e->set_source_id(input.at("device"));
      e->set_connector_id(input.at("device"));
      e->set_source_epoch(1);
      e->set_raw_code(input.at("seed"));
      e->set_type(input.value("type", "generic.alarm"));
      e->set_severity(psim::common::v1::SEVERITY_HIGH);
      e->set_significant(input.value("significant", false));
      e->set_taxonomy_version("1");
      if (input.value("padding", std::size_t{0}) > 0) {
        (*e->mutable_attributes())["x.evidence"] = std::string(input.at("padding").get<std::size_t>(), 'x');
      }
      key = e->site_id() + ":" + e->zone_id();
      record = std::move(event);
    } else if (mode == "signals") {
      auto signal = std::make_unique<psim::processing::v1::SignalRecord>();
      id = identity(input, "signal", i);
      envelope(signal->mutable_envelope(), input, id, base + static_cast<std::int64_t>(i));
      auto* s = signal->mutable_signal();
      s->set_site_id(input.at("site"));
      s->set_zone_id(input.at("zone"));
      s->set_grouping_key(input.at("seed"));
      s->set_correlation_key(input.at("seed"));
      s->set_incident_type_id(input.at("device"));
      s->set_priority(psim::common::v1::PRIORITY_P2);
      s->set_summary("History evidence");
      s->mutable_origin()->set_kind(input.value("external", false) ? psim::processing::v1::SIGNAL_ORIGIN_KIND_EXTERNAL
                                                                   : psim::processing::v1::SIGNAL_ORIGIN_KIND_RULE);
      s->mutable_origin()->set_rule_id(input.at("rule"));
      s->mutable_origin()->set_rule_version(static_cast<std::uint32_t>((i % 2) + 1));
      if (input.value("absence", false)) {
        *s->mutable_absence_deadline() = signal->envelope().occurred_at();
      } else {
        s->add_event_ids(identity(input, "event", i));
      }
      *s->mutable_first_event_at() = signal->envelope().occurred_at();
      *s->mutable_last_event_at() = s->first_event_at();
      key = s->grouping_key();
      record = std::move(signal);
    } else if (mode == "incidents") {
      auto event = std::make_unique<psim::incident::v1::IncidentEventRecord>();
      id = identity(input, "attachment", i);
      envelope(event->mutable_envelope(), input, id, base + static_cast<std::int64_t>(i));
      auto* change = event->mutable_signal_attached();
      change->set_signal_id(identity(input, "signal", i));
      auto* incident = change->mutable_incident();
      incident->set_incident_id(input.at("incident"));
      incident->set_site_id(input.at("site"));
      incident->set_zone_id(input.at("zone"));
      incident->set_version(i + 1);
      incident->set_state(psim::incident::v1::INCIDENT_STATE_NEW);
      key = incident->incident_id();
      record = std::move(event);
    } else {
      auto config = std::make_unique<psim::config::v1::ConfigRecord>();
      id = identity(input, "config", i);
      envelope(config->mutable_envelope(), input, id, base);
      auto* retention = config->mutable_retention();
      retention->set_version(1);
      retention->mutable_event_history_all()->set_seconds(input.value("allSeconds", 3));
      retention->mutable_event_history_significant()->set_seconds(input.value("significantSeconds", 12));
      key = "retention/" + input.at("tenant").get<std::string>();
      record = std::move(config);
    }
    p::kafka::wire::write_header(bytes, schema, p::kafka::wire::message_indexes(*record->GetDescriptor()));
    record->AppendToString(&bytes);
    for (int repeat = 0; repeat < input.value("repeat", 1); ++repeat) {
      take(co_await producer->enqueue({.topic = topic, .key = key, .value = bytes}));
    }
    if (count <= 10000) {
      ids.push_back(id);
    }
  }
  take(co_await producer->flush(std::chrono::seconds(60)));
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  std::cout << Json{{"firstZone", input.value("zones", std::size_t{1}) > 1
                                      ? p::stream::uuid5(input.at("zone").get<std::string>(), {"0"})
                                      : input.at("zone").get<std::string>()},
                    {"ids", ids},
                    {"count", count},
                    {"seconds", seconds},
                    {"perSecond", static_cast<double>(count) / seconds}}
                   .dump()
            << '\n';
  producer.reset();
  co_return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      return 2;
    }
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    std::ifstream file(arguments[1]);
    Json input;
    file >> input;
    asio::io_context loop;
    auto result = asio::co_spawn(loop, emit(std::move(input)), asio::use_future);
    loop.run();
    return result.get();
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
