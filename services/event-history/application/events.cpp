#include "events.hpp"

#include <clickhouse/columns/date.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "psim/common/v1/types.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"

namespace psim::event_history {
namespace {
using Time = google::protobuf::util::TimeUtil;
constexpr std::int64_t kDay = 86400;

bool valid(const processing::v1::EventRecord& record) {
  const auto& envelope = record.envelope();
  const auto& event = record.event();
  return record.has_event() && p::stream::uuid(envelope.tenant_id()) && p::stream::uuid(envelope.message_id())
         && p::stream::uuid(event.site_id()) && p::stream::uuid(event.zone_id()) && p::stream::uuid(event.device_id())
         && Time::IsTimestampValid(envelope.occurred_at()) && envelope.occurred_at().seconds() > 0
         && envelope.occurred_at().seconds() < 10000000000LL && !event.type().empty()
         && common::v1::Severity_IsValid(event.severity()) && event.severity() != 0;
}
}  // namespace

p::Result<void> Events::config(const config::v1::ConfigRecord& record) {
  if (!record.has_retention()) {
    return {};
  }
  const auto& value = record.retention();
  for (const auto& duration : {value.event_history_all(), value.event_history_significant()}) {
    if (duration.seconds() < 1 || duration.seconds() > 3650 * kDay || duration.nanos() != 0) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid,
                     "event retention must be whole seconds between one second and ten years");
    }
  }
  if (value.event_history_significant().seconds() < value.event_history_all().seconds()) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid,
                   "significant retention cannot be shorter than ordinary retention");
  }
  const auto& tenant = record.envelope().tenant_id();
  if (!p::stream::uuid(tenant) || (!retention_.contains(tenant) && retention_.size() >= 100000)) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid retention tenant or replica capacity exceeded");
  }
  if (!retention_.contains(tenant) || retention_.at(tenant).version() < value.version()) {
    retention_[tenant] = value;
  }
  return {};
}

p::Result<p::clickhouse::Converted> Events::convert_records(
    std::span<const processing::v1::EventRecord> records) const {
  using String = ::clickhouse::ColumnString;
  auto tenant = std::make_shared<String>();
  auto ids = std::make_shared<String>();
  auto site = std::make_shared<String>();
  auto zone = std::make_shared<String>();
  auto device = std::make_shared<String>();
  auto type = std::make_shared<String>();
  auto code = std::make_shared<String>();
  auto source = std::make_shared<String>();
  auto connector = std::make_shared<String>();
  auto attributes = std::make_shared<String>();
  auto payload = std::make_shared<String>();
  auto occurred = std::make_shared<::clickhouse::ColumnDateTime64>(3);
  auto received = std::make_shared<::clickhouse::ColumnDateTime64>(3);
  auto expires = std::make_shared<::clickhouse::ColumnDateTime64>(3);
  auto sequence = std::make_shared<::clickhouse::ColumnUInt64>();
  auto epoch = std::make_shared<::clickhouse::ColumnUInt32>();
  auto severity = std::make_shared<::clickhouse::ColumnUInt8>();
  auto significant = std::make_shared<::clickhouse::ColumnUInt8>();
  p::clickhouse::Converted result;
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto& record = records[index];
    if (!valid(record)) {
      result.rejected.push_back({index});
      continue;
    }
    const auto& envelope = record.envelope();
    const auto& event = record.event();
    const auto retention = retention_.find(envelope.tenant_id());
    auto seconds = (event.significant() ? 90 : 7) * kDay;
    if (retention != retention_.end()) {
      seconds = event.significant() ? retention->second.event_history_significant().seconds()
                                    : retention->second.event_history_all().seconds();
    }
    tenant->Append(envelope.tenant_id());
    ids->Append(envelope.message_id());
    site->Append(event.site_id());
    zone->Append(event.zone_id());
    device->Append(event.device_id());
    type->Append(event.type());
    code->Append(event.raw_code());
    source->Append(event.source_id());
    connector->Append(event.connector_id());
    nlohmann::json attrs = nlohmann::json::object();
    for (const auto& [name, value] : event.attributes()) {
      attrs[name] = value;
    }
    attributes->Append(attrs.dump());
    payload->Append(record.SerializeAsString());
    const auto millis = Time::TimestampToMilliseconds(envelope.occurred_at());
    occurred->Append(millis);
    received->Append(Time::TimestampToMilliseconds(envelope.received_at()));
    expires->Append(millis + (seconds * 1000));
    sequence->Append(envelope.sequence());
    epoch->Append(event.source_epoch());
    severity->Append(static_cast<std::uint8_t>(event.severity()));
    significant->Append(event.significant() ? 1 : 0);
  }
  auto& block = result.block;
  block.AppendColumn("tenant_id", tenant);
  block.AppendColumn("event_id", ids);
  block.AppendColumn("site_id", site);
  block.AppendColumn("zone_id", zone);
  block.AppendColumn("device_id", device);
  block.AppendColumn("type", type);
  block.AppendColumn("raw_code", code);
  block.AppendColumn("source_id", source);
  block.AppendColumn("connector_id", connector);
  block.AppendColumn("attributes", attributes);
  block.AppendColumn("payload", payload);
  block.AppendColumn("occurred_at", occurred);
  block.AppendColumn("received_at", received);
  block.AppendColumn("expires_at", expires);
  block.AppendColumn("source_sequence", sequence);
  block.AppendColumn("source_epoch", epoch);
  block.AppendColumn("severity", severity);
  block.AppendColumn("significant", significant);
  return result;
}

p::Result<p::clickhouse::Converted> Events::convert(std::span<const p::kafka::Message* const> messages) const {
  std::vector<processing::v1::EventRecord> records;
  records.reserve(messages.size());
  const p::kafka::ProtobufSerde<processing::v1::EventRecord> serde(1);
  for (const auto* message : messages) {
    auto record = serde.deserialize(message->value());
    records.push_back(record ? std::move(*record) : processing::v1::EventRecord{});
  }
  return convert_records(records);
}

p::clickhouse::Migration migration(const std::string& cluster) {
  if (cluster.empty() || cluster.size() > 64
      || !std::ranges::all_of(cluster, [](unsigned char ch) { return std::isalnum(ch) != 0 || ch == '_'; })) {
    return {.version = 1, .name = "invalid_cluster"};
  }
  // psim-arch: allow-sql (cluster is a validated deployment identifier; all other SQL is constant)
  const auto ddl = "CREATE TABLE IF NOT EXISTS events ON CLUSTER " + cluster + R"(
(tenant_id String, event_id String, site_id String, zone_id String, device_id String,
 type LowCardinality(String), raw_code LowCardinality(String), source_id String, connector_id String,
 attributes String, payload String, occurred_at DateTime64(3, 'UTC'), received_at DateTime64(3, 'UTC'),
 expires_at DateTime64(3, 'UTC'), source_sequence UInt64, source_epoch UInt32, severity UInt8, significant UInt8,
 INDEX events_device device_id TYPE bloom_filter GRANULARITY 4,
 INDEX events_type type TYPE set(1024) GRANULARITY 4,
 INDEX events_id event_id TYPE bloom_filter GRANULARITY 4)
ENGINE=ReplicatedReplacingMergeTree('/clickhouse/psim/{shard}/{database}/events', '{replica}')
PARTITION BY toYYYYMMDD(occurred_at)
ORDER BY (tenant_id, site_id, zone_id, occurred_at, event_id)
TTL toDateTime(expires_at) DELETE
SETTINGS replicated_deduplication_window=100000, replicated_deduplication_window_seconds=604800)";
  return {.version = 1, .name = "event_history_events", .expand = {ddl}};
}
}  // namespace psim::event_history
