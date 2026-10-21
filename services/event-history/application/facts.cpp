#include "facts.hpp"

#include <clickhouse/block.h>
#include <clickhouse/columns/array.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string_view>

#include "events.hpp"
#include "google/protobuf/timestamp.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/incident/v1/events.pb.h"
#include "psim/incident/v1/incident.pb.h"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/signal.pb.h"

namespace psim::event_history {
namespace {
using Time = google::protobuf::util::TimeUtil;

struct Columns {
  std::shared_ptr<::clickhouse::ColumnString> tenant = std::make_shared<::clickhouse::ColumnString>(),
                                              id = std::make_shared<::clickhouse::ColumnString>(),
                                              kind = std::make_shared<::clickhouse::ColumnString>(),
                                              signal = std::make_shared<::clickhouse::ColumnString>(),
                                              incident = std::make_shared<::clickhouse::ColumnString>(),
                                              site = std::make_shared<::clickhouse::ColumnString>(),
                                              zone = std::make_shared<::clickhouse::ColumnString>(),
                                              rule = std::make_shared<::clickhouse::ColumnString>();
  std::shared_ptr<::clickhouse::ColumnUInt32> version = std::make_shared<::clickhouse::ColumnUInt32>();
  std::shared_ptr<::clickhouse::ColumnArray> events =
      std::make_shared<::clickhouse::ColumnArray>(std::make_shared<::clickhouse::ColumnString>());
  std::shared_ptr<::clickhouse::ColumnDateTime64> occurred = std::make_shared<::clickhouse::ColumnDateTime64>(3),
                                                  expires = std::make_shared<::clickhouse::ColumnDateTime64>(3);

  ::clickhouse::Block block() const {
    ::clickhouse::Block b;
    b.AppendColumn("tenant_id", tenant);
    b.AppendColumn("fact_id", id);
    b.AppendColumn("kind", kind);
    b.AppendColumn("signal_id", signal);
    b.AppendColumn("incident_id", incident);
    b.AppendColumn("site_id", site);
    b.AppendColumn("zone_id", zone);
    b.AppendColumn("rule_id", rule);
    b.AppendColumn("rule_version", version);
    b.AppendColumn("event_ids", events);
    b.AppendColumn("occurred_at", occurred);
    b.AppendColumn("expires_at", expires);
    return b;
  }
};

bool timestamp(const google::protobuf::Timestamp& t) {
  return t.seconds() > 0 && t.seconds() < 10000000000LL && t.nanos() >= 0 && t.nanos() < 1000000000;
}

void append(Columns& c, const processing::v1::SignalRecord& r, std::int64_t retention) {
  const auto& e = r.envelope();
  const auto& s = r.signal();
  c.tenant->Append(e.tenant_id());
  c.id->Append(e.message_id());
  c.kind->Append("signal");
  c.signal->Append(e.message_id());
  c.incident->Append("");
  c.site->Append(s.site_id());
  c.zone->Append(s.zone_id());
  c.rule->Append(s.origin().kind() == processing::v1::SIGNAL_ORIGIN_KIND_RULE ? s.origin().rule_id() : "");
  c.version->Append(s.origin().kind() == processing::v1::SIGNAL_ORIGIN_KIND_RULE ? s.origin().rule_version() : 0);
  auto ids = std::make_shared<::clickhouse::ColumnString>();
  const std::set<std::string> unique(s.event_ids().begin(), s.event_ids().end());
  for (const auto& id : unique) {
    ids->Append(id);
  }
  c.events->AppendAsColumn(ids);
  const auto when = Time::TimestampToMilliseconds(e.occurred_at());
  c.occurred->Append(when);
  c.expires->Append(when + (retention * 1000));
}
}  // namespace

bool valid_signal(const processing::v1::SignalRecord& r) {
  const auto& s = r.signal();
  const auto& e = r.envelope();
  if (!r.has_signal() || !p::stream::uuid(e.tenant_id()) || !p::stream::uuid(e.message_id())
      || !p::stream::uuid(s.site_id()) || (!s.zone_id().empty() && !p::stream::uuid(s.zone_id()))
      || !timestamp(e.occurred_at()) || (s.has_first_event_at() && !timestamp(s.first_event_at()))
      || (s.has_last_event_at() && !timestamp(s.last_event_at()))
      || (s.has_first_event_at() && s.has_last_event_at()
          && Time::TimestampToMilliseconds(s.first_event_at()) > Time::TimestampToMilliseconds(s.last_event_at()))
      || s.event_ids_size() > 1000 || (s.event_ids().empty() && !s.has_absence_deadline())
      || (s.origin().kind() != processing::v1::SIGNAL_ORIGIN_KIND_RULE
          && s.origin().kind() != processing::v1::SIGNAL_ORIGIN_KIND_EXTERNAL)
      || (s.origin().kind() == processing::v1::SIGNAL_ORIGIN_KIND_RULE
          && (!p::stream::uuid(s.origin().rule_id()) || s.origin().rule_version() == 0))) {
    return false;
  }
  return std::ranges::all_of(s.event_ids(), [](const auto& id) { return p::stream::uuid(id); });
}

p::Result<void> Facts::config(const config::v1::ConfigRecord& r) {
  if (!r.has_retention()) {
    return {};
  }
  const auto& t = r.retention().event_history_significant();
  const auto& all = r.retention().event_history_all();
  if (!p::stream::uuid(r.envelope().tenant_id()) || t.seconds() < 1 || t.seconds() > 315360000 || t.nanos() != 0
      || all.nanos() != 0 || all.seconds() < 1 || all.seconds() > t.seconds()
      || (!retention_.contains(r.envelope().tenant_id()) && retention_.size() >= 100000)) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid signal retention");
  }
  auto& item = retention_[r.envelope().tenant_id()];
  if (item.first < r.retention().version() || item.second == 0) {
    item = {r.retention().version(), t.seconds()};
  }
  return {};
}

std::int64_t Facts::retention(std::string_view tenant) const {
  const auto found = retention_.find(std::string(tenant));
  return found == retention_.end() ? 7776000 : found->second.second;
}

p::Result<p::clickhouse::Converted> Facts::signals(std::span<const processing::v1::SignalRecord> records) const {
  Columns c;
  p::clickhouse::Converted result;
  for (std::size_t i = 0; i < records.size(); ++i) {
    if (!valid_signal(records[i])) {
      result.rejected.push_back({i});
    } else {
      append(c, records[i], retention(records[i].envelope().tenant_id()));
    }
  }
  result.block = c.block();
  return result;
}

p::Result<p::clickhouse::Converted> Facts::convert(std::span<const p::kafka::Message* const> messages) const {
  Columns c;
  p::clickhouse::Converted result;
  for (std::size_t i = 0; i < messages.size(); ++i) {
    const auto* m = messages[i];
    if (m->topic() == "psim.signals.v1") {
      auto r = p::kafka::ProtobufSerde<processing::v1::SignalRecord>(1).deserialize(m->value());
      if (!r || !valid_signal(*r)) {
        result.rejected.push_back({i});
        continue;
      }
      append(c, *r, retention(r->envelope().tenant_id()));
      continue;
    }
    auto r = p::kafka::ProtobufSerde<incident::v1::IncidentEventRecord>(1).deserialize(m->value());
    if (!r || !p::stream::uuid(r->envelope().tenant_id()) || !p::stream::uuid(r->envelope().message_id())
        || !timestamp(r->envelope().occurred_at())) {
      result.rejected.push_back({i});
      continue;
    }
    const auto* field = incident::v1::IncidentEventRecord::GetReflection()->GetOneofFieldDescriptor(
        *r, incident::v1::IncidentEventRecord::descriptor()->FindOneofByName("payload"));
    if (!field) {
      result.rejected.push_back({i});
      continue;
    }
    const auto& change = incident::v1::IncidentEventRecord::GetReflection()->GetMessage(*r, field);
    const auto* snapshot = change.GetDescriptor()->FindFieldByName("incident");
    incident::v1::Incident value;
    if (snapshot) {
      value.CopyFrom(change.GetReflection()->GetMessage(change, snapshot));
    }
    std::string signal;
    if (r->has_incident_created()) {
      signal = r->incident_created().signal_id();
    }
    if (r->has_signal_attached()) {
      signal = r->signal_attached().signal_id();
    }
    if (!p::stream::uuid(value.incident_id()) || !p::stream::uuid(value.site_id())
        || (!signal.empty() && !p::stream::uuid(signal))) {
      result.rejected.push_back({i});
      continue;
    }
    c.tenant->Append(r->envelope().tenant_id());
    c.id->Append(r->envelope().message_id());
    c.kind->Append("incident");
    c.signal->Append(signal);
    c.incident->Append(value.incident_id());
    c.site->Append(value.site_id());
    c.zone->Append(value.zone_id());
    c.rule->Append("");
    c.version->Append(0);
    c.events->AppendAsColumn(std::make_shared<::clickhouse::ColumnString>());
    const auto when = Time::TimestampToMilliseconds(r->envelope().occurred_at());
    c.occurred->Append(when);
    c.expires->Append(when + (retention(r->envelope().tenant_id()) * 1000));
  }
  result.block = c.block();
  return result;
}

p::clickhouse::Migration facts_migration(const std::string& cluster) {
  if (migration(cluster).expand.empty()) {
    return {.version = 2, .name = "invalid_cluster"};
  }
  // psim-arch: allow-sql (cluster is a validated deployment identifier)
  const auto facts = "CREATE TABLE IF NOT EXISTS history_facts ON CLUSTER " + cluster + R"sql(
(tenant_id String,fact_id String,kind LowCardinality(String),signal_id String,incident_id String,
 site_id String,zone_id String,rule_id String,rule_version UInt32,event_ids Array(String),
 occurred_at DateTime64(3,'UTC'),expires_at DateTime64(3,'UTC'),
 retention_version UInt64 DEFAULT toUInt64(toUnixTimestamp64Milli(expires_at)),
 INDEX history_signal signal_id TYPE bloom_filter GRANULARITY 4,
 INDEX history_incident incident_id TYPE bloom_filter GRANULARITY 4)
ENGINE=ReplicatedReplacingMergeTree('/clickhouse/psim/{shard}/{database}/history_facts','{replica}',retention_version)
PARTITION BY toYYYYMMDD(occurred_at) ORDER BY(tenant_id,kind,site_id,zone_id,occurred_at,fact_id)
TTL toDateTime(expires_at) DELETE
SETTINGS replicated_deduplication_window=100000,replicated_deduplication_window_seconds=604800)sql";
  // psim-arch: allow-sql (cluster is a validated deployment identifier)
  const auto signals = "CREATE VIEW IF NOT EXISTS signal_facts ON CLUSTER " + cluster
                       + " AS SELECT * FROM history_facts FINAL WHERE kind='signal'";
  // psim-arch: allow-sql (cluster is a validated deployment identifier)
  const auto links="CREATE VIEW IF NOT EXISTS event_signals ON CLUSTER "+cluster+" AS SELECT tenant_id,signal_id,site_id,zone_id,occurred_at,expires_at,arrayJoin(event_ids) AS event_id FROM signal_facts";
  // psim-arch: allow-sql (cluster is a validated deployment identifier)
  const auto incidents="CREATE VIEW IF NOT EXISTS incident_signals ON CLUSTER "+cluster+" AS SELECT DISTINCT tenant_id,incident_id,signal_id,site_id,zone_id,occurred_at,expires_at FROM history_facts FINAL WHERE kind='incident' AND signal_id!=''";
  // psim-arch: allow-sql (cluster is a validated deployment identifier)
  const auto retained = "CREATE TABLE IF NOT EXISTS retained_events ON CLUSTER " + cluster + R"sql(
(tenant_id String,event_id String,site_id String,zone_id String,device_id String,type LowCardinality(String),
raw_code LowCardinality(String),source_id String,connector_id String,attributes String,payload String,
occurred_at DateTime64(3,'UTC'),received_at DateTime64(3,'UTC'),expires_at DateTime64(3,'UTC'),
source_sequence UInt64,source_epoch UInt32,severity UInt8,significant UInt8,retention_version UInt64)
ENGINE=ReplicatedReplacingMergeTree('/clickhouse/psim/{shard}/{database}/retained_events','{replica}',retention_version)
PARTITION BY toYYYYMMDD(occurred_at) ORDER BY(tenant_id,site_id,zone_id,occurred_at,event_id)
TTL toDateTime(expires_at) DELETE
SETTINGS replicated_deduplication_window=100000,replicated_deduplication_window_seconds=604800)sql";
  return {.version = 2,
          .name = "signal facts and retained evidence",
          .expand = {facts, signals, links, incidents, retained}};
}
}  // namespace psim::event_history
