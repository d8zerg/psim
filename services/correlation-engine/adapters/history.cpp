#include "history.hpp"

#include <clickhouse/columns/string.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "psim/api/v1/rules.pb.h"
#include "psim/audit/v1/audit.pb.h"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/catalog/v1/maintenance.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"
#include "repository.hpp"
#include "rules.hpp"

namespace psim::correlation::management {
using Time = google::protobuf::util::TimeUtil;
using Code = p::ErrorCode;

namespace {
template <typename T>
p::Result<T> parse(const Json& body) {
  T value;
  if (!google::protobuf::util::JsonStringToMessage(body.dump(), &value).ok()) {
    return p::fail(Code::kProcessingPayloadInvalid, "invalid historical snapshot");
  }
  return value;
}

bool scoped(const Job& job, const std::string& site) {
  const auto scopes = job.principal.at("scopes").get<std::set<std::string>>();
  return scopes.contains("*") || scopes.contains(site);
}
}  // namespace

Json snapshot(const HistoricalInput& input) {
  Json value{{"events", Json::array()},
             {"locations", Json::array()},
             {"maintenance", Json::array()},
             {"subjects", Json::array()}};
  for (const auto& event : input.events) {
    value["events"].push_back(Json::parse(p::stream::json(event)));
  }
  for (const auto& [id, location] : input.context.locations) {
    (void)id;
    value["locations"].push_back(Json::parse(p::stream::json(location)));
  }
  for (const auto& maintenance : input.context.maintenance) {
    value["maintenance"].push_back(Json::parse(p::stream::json(maintenance)));
  }
  for (const auto& subject : input.context.subjects) {
    value["subjects"].push_back(Json::parse(p::stream::json(subject)));
  }
  return value;
}

p::Result<HistoricalInput> restore_input(const Json& input) {
  HistoricalInput result;
  for (const auto& item : input.at("events")) {
    auto event = parse<rules::Event>(item);
    if (!event) {
      return std::unexpected(event.error());
    }
    result.events.push_back(std::move(*event));
  }
  for (const auto& item : input.at("locations")) {
    auto location = parse<catalog::v1::Location>(item);
    if (!location) {
      return std::unexpected(location.error());
    }
    result.context.locations[location->location_id()] = std::move(*location);
  }
  for (const auto& item : input.at("maintenance")) {
    auto maintenance = parse<catalog::v1::MaintenanceWindow>(item);
    if (!maintenance) {
      return std::unexpected(maintenance.error());
    }
    result.context.maintenance.push_back(std::move(*maintenance));
  }
  for (const auto& item : input.at("subjects")) {
    auto event = parse<rules::Event>(item);
    if (!event) {
      return std::unexpected(event.error());
    }
    result.context.subjects.push_back(std::move(*event));
  }
  return result;
}

asio::awaitable<p::Result<HistoricalInput>> History::read(Job job) {
  auto request = parse<api::v1::StartDryRunRequest>(job.request);
  if (!request) {
    co_return std::unexpected(request.error());
  }
  const auto from = Time::TimestampToMilliseconds(request->from());
  const auto to = Time::TimestampToMilliseconds(request->to());
  const bool kafka = request->source() == api::v1::DRY_RUN_SOURCE_KAFKA;
  if (kafka && from < Time::TimestampToMilliseconds(Time::GetCurrentTime()) - 86400000 + 30000) {
    co_return p::fail(Code::kProcessingDryRunRangeInvalid,
                      "Kafka range exceeds normalized-event retention; use ClickHouse");
  }
  std::vector<std::string> topics{"psim.catalog.v1", "psim.config.v1", "psim.audit.v1"};
  if (kafka) {
    topics.emplace_back("psim.events.normalized.v1");
  }
  auto consumer = p::kafka::Consumer::create(executor_,
                                             {.client = options_,
                                              .group_id = "rule-replay-" + job.id,
                                              .topics = topics,
                                              .group_protocol = "classic",
                                              .manual = true},
                                             dependencies_);
  if (!consumer) {
    co_return std::unexpected(consumer.error());
  }
  auto bounds = co_await (*consumer)->offset_bounds(std::chrono::seconds(5));
  if (!bounds) {
    co_return std::unexpected(bounds.error());
  }
  auto assigned = (*consumer)->assign(bounds->begin);
  if (!assigned) {
    co_return std::unexpected(assigned.error());
  }
  HistoricalInput input;
  std::map<std::string, catalog::v1::Device> devices;
  std::map<std::pair<std::string, std::uint64_t>, catalog::v1::MaintenanceWindow> maintenance;
  std::map<p::kafka::TopicPartition, std::int64_t> first_receipt;
  std::size_t scanned = 0;
  std::size_t bytes = 0;
  auto append = [&](rules::Event event) -> p::Result<void> {
    if (event.envelope().tenant_id() != job.tenant) {
      return {};
    }
    if (!event.has_event() || !event.envelope().has_occurred_at()
        || !Time::IsTimestampValid(event.envelope().occurred_at())
        || (event.envelope().has_received_at() && !Time::IsTimestampValid(event.envelope().received_at()))) {
      return p::fail(Code::kProcessingPayloadInvalid, "invalid historical event timestamp");
    }
    const auto time = Time::TimestampToMilliseconds(event.envelope().occurred_at());
    if (event.envelope().tenant_id() != job.tenant || time < from || time >= to
        || !scoped(job, event.event().site_id())) {
      return {};
    }
    bytes += event.ByteSizeLong();
    if (input.events.size() >= 100000 || bytes > 16777216) {
      return p::fail(Code::kCommonRateLimited, "historical input exceeds replay limits");
    }
    input.events.push_back(std::move(event));
    return {};
  };
  auto apply = [&](const p::kafka::Message& message) -> p::Result<void> {
    if (++scanned > 1000000) {
      return p::fail(Code::kCommonRateLimited, "historical scan limit exceeded");
    }
    if (message.value().empty()) {
      return {};
    }
    if (message.topic() == "psim.events.normalized.v1") {
      auto event = p::kafka::ProtobufSerde<rules::Event>(1).deserialize(message.value());
      if (!event) {
        return std::unexpected(event.error());
      }
      if (!first_receipt.contains(message.topic_partition())) {
        if (!event->envelope().has_received_at() || !Time::IsTimestampValid(event->envelope().received_at())) {
          return p::fail(Code::kProcessingPayloadInvalid, "historical event has no receipt time");
        }
        first_receipt[message.topic_partition()] = Time::TimestampToMilliseconds(event->envelope().received_at());
      }
      return append(std::move(*event));
    }
    if (message.topic() == "psim.catalog.v1") {
      auto value = p::kafka::ProtobufSerde<catalog::v1::CatalogRecord>(1).deserialize(message.value());
      if (!value) {
        return std::unexpected(value.error());
      }
      if (value->envelope().tenant_id() != job.tenant) {
        return {};
      }
      if (value->has_location() && scoped(job, value->location().site_id())) {
        auto& old = input.context.locations[value->location().location_id()];
        if (value->location().version() > old.version()) {
          old = value->location();
        }
      }
      if (value->has_device() && scoped(job, value->device().site_id())) {
        auto& old = devices[value->device().device_id()];
        if (value->device().version() > old.version()) {
          old = value->device();
        }
      }
      if (devices.size() + input.context.locations.size() > 50000) {
        return p::fail(Code::kCommonRateLimited, "historical catalog limit exceeded");
      }
      return {};
    }
    if (message.topic() == "psim.config.v1") {
      auto value = p::kafka::ProtobufSerde<config::v1::ConfigRecord>(1).deserialize(message.value());
      if (!value) {
        return std::unexpected(value.error());
      }
      if (value->envelope().tenant_id() == job.tenant && value->has_maintenance_window()
          && scoped(job, value->maintenance_window().site_id())) {
        const auto& policy = value->maintenance_window();
        maintenance[{policy.maintenance_id(), policy.version()}] = policy;
      }
      return {};
    }
    auto value = p::kafka::ProtobufSerde<audit::v1::AuditEventRecord>(1).deserialize(message.value());
    if (!value) {
      return std::unexpected(value.error());
    }
    if (value->envelope().tenant_id() != job.tenant || !value->has_audit_entry()) {
      return {};
    }
    const auto& entry = value->audit_entry();
    if (entry.object_type() == "maintenance" && entry.action().starts_with("catalog.maintenance.")
        && scoped(job, entry.site_id()) && entry.outcome() == audit::v1::AUDIT_OUTCOME_SUCCESS
        && entry.details().contains("snapshot")) {
      auto policy = parse<catalog::v1::MaintenanceWindow>(Json::parse(entry.details().at("snapshot")));
      if (!policy) {
        return std::unexpected(policy.error());
      }
      maintenance[{policy->maintenance_id(), policy->version()}] = std::move(*policy);
      if (maintenance.size() > 10000) {
        return p::fail(Code::kCommonRateLimited, "maintenance history limit exceeded");
      }
    }
    return {};
  };
  p::Result<void> status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
  while (true) {
    if (stopping_) {
      status = p::fail(Code::kCommonUnavailable, "historical reader is stopping");
      break;
    }
    const auto& positions = (*consumer)->positions();
    if (std::ranges::all_of(bounds->end, [&](const auto& part) {
          auto pos = positions.find(part.first);
          return pos != positions.end() && pos->second >= part.second;
        })) {
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      status = p::fail(Code::kCommonDeadlineExceeded, "historical read timed out");
      break;
    }
    auto batch = co_await (*consumer)->poll(1000, std::chrono::milliseconds(100));
    if (!batch) {
      status = std::unexpected(batch.error());
      break;
    }
    for (const auto& message : *batch) {
      if (message.offset() >= bounds->end.at(message.topic_partition())) {
        continue;
      }
      status = apply(message);
      if (!status) {
        break;
      }
    }
    if (!status) {
      break;
    }
  }
  co_await (*consumer)->close();
  if (!status) {
    co_return std::unexpected(status.error());
  }
  for (const auto& [partition, low] : bounds->begin) {
    if (partition.topic == "psim.events.normalized.v1" && low > 0
        && (!first_receipt.contains(partition) || from < first_receipt.at(partition) + 30000)) {
      co_return p::fail(Code::kProcessingDryRunRangeInvalid, "Kafka retention does not cover the requested range");
    }
  }
  for (const auto& [identity, policy] : maintenance) {
    (void)identity;
    input.context.maintenance.push_back(policy);
  }
  for (const auto& [id, device] : devices) {
    (void)id;
    if (device.state() != catalog::v1::RESOURCE_STATE_ACTIVE) {
      continue;
    }
    for (const auto& source : device.sources()) {
      rules::Event subject;
      subject.mutable_envelope()->set_tenant_id(job.tenant);
      auto* event = subject.mutable_event();
      event->set_site_id(device.site_id());
      event->set_zone_id(device.zone_id());
      event->set_device_id(device.device_id());
      event->set_connector_id(device.connector_id());
      event->set_source_id(source.source_id());
      input.context.subjects.push_back(std::move(subject));
      if (input.context.subjects.size() > 10000) {
        co_return p::fail(Code::kCommonRateLimited, "inventory replay limit exceeded");
      }
    }
  }
  if (!kafka) {
    auto rows = co_await clickhouse_->select({.table = "events",
                                              .columns = {"payload"},
                                              .tenant = job.tenant,
                                              .from_ms = from,
                                              .to_ms = to,
                                              .limit = 100001});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    if (rows->rows > 100000) {
      co_return p::fail(Code::kCommonRateLimited, "ClickHouse replay limit exceeded");
    }
    for (const auto& block : rows->blocks) {
      const auto payload = block[0]->As<::clickhouse::ColumnString>();
      if (!payload) {
        co_return p::fail(Code::kProcessingPayloadInvalid, "invalid history payload column");
      }
      for (std::size_t row = 0; row < block.GetRowCount(); ++row) {
        rules::Event event;
        const auto value = payload->At(row);
        if (!event.ParseFromString(std::string(value))) {
          co_return p::fail(Code::kProcessingPayloadInvalid, "invalid stored history event");
        }
        auto added = append(std::move(event));
        if (!added) {
          co_return std::unexpected(added.error());
        }
      }
    }
  }
  co_return input;
}
}  // namespace psim::correlation::management
