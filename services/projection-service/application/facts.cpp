#include "facts.hpp"

#include <clickhouse/block.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>
#include <psim/incident/v1/events.pb.h>
#include <psim/incident/v1/incident.pb.h>
#include <psim/response/v1/events.pb.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::projection {
namespace {
using Time = google::protobuf::util::TimeUtil;

bool timestamp(const google::protobuf::Timestamp& value) {
  return value.seconds() > 0 && value.seconds() < 10000000000LL && value.nanos() >= 0 && value.nanos() < 1000000000;
}

bool duration(const google::protobuf::Duration& value) {
  return value.seconds() >= 0 && value.seconds() <= 31536000 && value.nanos() >= 0 && value.nanos() < 1000000000
         && Time::DurationToMilliseconds(value) > 0;
}
}  // namespace

p::Result<Fact> fact(std::string tenant, const incident::v1::Incident& i) {
  if (!p::stream::uuid(tenant) || !p::stream::uuid(i.incident_id()) || !p::stream::uuid(i.site_id())
      || !p::stream::uuid(i.incident_type_id()) || (!i.zone_id().empty() && !p::stream::uuid(i.zone_id()))
      || i.assignee_id().size() > 256 || i.version() == 0 || !timestamp(i.created_at()) || i.priority() < 1
      || i.priority() > 4 || i.state() < 1 || i.state() > 5 || i.resolution() < 0 || i.resolution() > 4
      || (i.has_acknowledged_at() && !timestamp(i.acknowledged_at()))
      || (i.has_resolved_at() && !timestamp(i.resolved_at()))) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid incident fact");
  }
  if ((i.state() >= 2 && !i.has_acknowledged_at()) || (i.state() >= 4 && !i.has_resolved_at())
      || (i.has_acknowledged_at()
          && Time::TimestampToMilliseconds(i.acknowledged_at()) < Time::TimestampToMilliseconds(i.created_at()))
      || (i.has_resolved_at()
          && (!i.has_acknowledged_at() || i.resolution() == 0
              || Time::TimestampToMilliseconds(i.resolved_at())
                     < Time::TimestampToMilliseconds(i.acknowledged_at())))) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid incident lifecycle timestamps");
  }
  return Fact{.tenant = std::move(tenant),
              .incident = i.incident_id(),
              .site = i.site_id(),
              .zone = i.zone_id(),
              .type = i.incident_type_id(),
              .assignee = i.assignee_id(),
              .version = i.version(),
              .created = Time::TimestampToMilliseconds(i.created_at()),
              .acknowledged = i.has_acknowledged_at() ? Time::TimestampToMilliseconds(i.acknowledged_at()) : 0,
              .resolved = i.has_resolved_at() ? Time::TimestampToMilliseconds(i.resolved_at()) : 0,
              .escalation = i.escalation_level(),
              .priority = static_cast<std::uint8_t>(i.priority()),
              .state = static_cast<std::uint8_t>(i.state()),
              .resolution = static_cast<std::uint8_t>(i.resolution())};
}

p::Result<Fact> fact(std::string tenant, const response::v1::SlaPolicyPinned& t) {
  if (!p::stream::uuid(tenant) || !p::stream::uuid(t.incident_id()) || !timestamp(t.created_at())
      || !duration(t.acknowledge_within()) || !duration(t.resolve_within())) {
    return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid SLA fact");
  }
  return Fact{.tenant = std::move(tenant),
              .incident = t.incident_id(),
              .version = 1,
              .created = Time::TimestampToMilliseconds(t.created_at()),
              .ack_target = Time::DurationToMilliseconds(t.acknowledge_within()),
              .resolve_target = Time::DurationToMilliseconds(t.resolve_within()),
              .kind = 2};
}

::clickhouse::Block block(std::span<const Fact> facts) {
  ::clickhouse::Block result;
  const auto strings = [&](const char* name, auto member) {
    auto column = std::make_shared<::clickhouse::ColumnString>();
    for (const auto& f : facts) {
      column->Append(f.*member);
    }
    result.AppendColumn(name, column);
  };
  strings("tenant_id", &Fact::tenant);
  strings("incident_id", &Fact::incident);
  strings("site_id", &Fact::site);
  strings("zone_id", &Fact::zone);
  strings("type_id", &Fact::type);
  strings("assignee_id", &Fact::assignee);
  const auto numbers = [&]<class Column>(const char* name, auto member) {
    auto column = std::make_shared<Column>();
    for (const auto& f : facts) {
      column->Append(f.*member);
    }
    result.AppendColumn(name, column);
  };
  numbers.template operator()<::clickhouse::ColumnUInt64>("version", &Fact::version);
  numbers.template operator()<::clickhouse::ColumnUInt8>("kind", &Fact::kind);
  numbers.template operator()<::clickhouse::ColumnUInt8>("priority", &Fact::priority);
  numbers.template operator()<::clickhouse::ColumnUInt8>("state", &Fact::state);
  numbers.template operator()<::clickhouse::ColumnUInt8>("resolution", &Fact::resolution);
  numbers.template operator()<::clickhouse::ColumnUInt32>("escalation_level", &Fact::escalation);
  numbers.template operator()<::clickhouse::ColumnInt64>("ack_ms", &Fact::acknowledged);
  numbers.template operator()<::clickhouse::ColumnInt64>("resolved_ms", &Fact::resolved);
  numbers.template operator()<::clickhouse::ColumnInt64>("ack_target_ms", &Fact::ack_target);
  numbers.template operator()<::clickhouse::ColumnInt64>("resolve_target_ms", &Fact::resolve_target);
  auto created = std::make_shared<::clickhouse::ColumnDateTime64>(3);
  for (const auto& f : facts) {
    created->Append(f.created);
  }
  result.AppendColumn("created_at", created);
  return result;
}

p::Result<p::clickhouse::Converted> convert(std::span<const p::kafka::Message* const> messages) {
  p::clickhouse::Converted output;
  std::vector<Fact> facts;
  for (std::size_t index = 0; index < messages.size(); ++index) {
    const auto& m = *messages[index];
    p::Result<Fact> value = p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid fact source");
    if (m.topic() == "psim.incidents.events.v1") {
      auto r = p::kafka::ProtobufSerde<incident::v1::IncidentEventRecord>(1).deserialize(m.value());
      if (r && p::stream::uuid(r->envelope().message_id()) && timestamp(r->envelope().occurred_at())) {
        const auto* field = incident::v1::IncidentEventRecord::GetReflection()->GetOneofFieldDescriptor(
            *r, incident::v1::IncidentEventRecord::GetDescriptor()->oneof_decl(0));
        if (field) {
          const auto& payload = incident::v1::IncidentEventRecord::GetReflection()->GetMessage(*r, field);
          const auto* snapshot = payload.GetDescriptor()->FindFieldByName("incident");
          if (snapshot) {
            incident::v1::Incident i;
            i.CopyFrom(payload.GetReflection()->GetMessage(payload, snapshot));
            if (m.key() == i.incident_id() && r->envelope().sequence() == i.version()) {
              value = fact(r->envelope().tenant_id(), i);
            }
          }
        }
      }
    } else if (m.topic() == "psim.response.events.v1") {
      auto r = p::kafka::ProtobufSerde<response::v1::ResponseEventRecord>(1).deserialize(m.value());
      if (r && p::stream::uuid(r->envelope().tenant_id()) && p::stream::uuid(r->envelope().message_id())
          && timestamp(r->envelope().occurred_at())
          && r->payload_case() != response::v1::ResponseEventRecord::PAYLOAD_NOT_SET) {
        if (!r->has_sla_policy_pinned()) {
          output.ignored.push_back(index);
          continue;
        }
        if (m.key() == r->sla_policy_pinned().incident_id()) {
          value = fact(r->envelope().tenant_id(), r->sla_policy_pinned());
        }
      }
    }
    if (value) {
      facts.push_back(std::move(*value));
    } else {
      output.rejected.push_back({index});
    }
  }
  output.block = block(facts);
  return output;
}

p::clickhouse::Migration migration(const std::string& cluster) {
  if (cluster.empty() || cluster.size() > 64 || !std::ranges::all_of(cluster, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
      })) {
    return {.version = 1, .name = "invalid_cluster"};
  }
  // psim-arch: allow-sql (validated deployment identifier)
  const auto raw = "CREATE TABLE IF NOT EXISTS incident_facts ON CLUSTER " + cluster + R"sql(
(tenant_id String,incident_id String,site_id String,zone_id String,type_id String,assignee_id String,
version UInt64,kind UInt8,priority UInt8,state UInt8,resolution UInt8,escalation_level UInt32,
ack_ms Int64,resolved_ms Int64,ack_target_ms Int64,resolve_target_ms Int64,created_at DateTime64(3,'UTC'))
ENGINE=ReplicatedReplacingMergeTree('/clickhouse/psim/{shard}/{database}/incident_facts','{replica}',version)
PARTITION BY toYYYYMM(created_at) ORDER BY(tenant_id,created_at,incident_id,kind)
TTL toDateTime(created_at)+INTERVAL 400 DAY DELETE
SETTINGS replicated_deduplication_window=100000,replicated_deduplication_window_seconds=604800)sql";
  // psim-arch: allow-sql (validated deployment identifier)
  const auto states = "CREATE TABLE IF NOT EXISTS incident_report_states ON CLUSTER " + cluster + R"sql(
(tenant_id String,incident_id String,created_at DateTime64(3,'UTC'),
incident AggregateFunction(argMax,Tuple(String,String,String,String,UInt8,UInt8,UInt8,Int64,Int64,UInt32,UInt64),UInt64),
sla AggregateFunction(argMax,Tuple(Int64,Int64),UInt64))
ENGINE=ReplicatedAggregatingMergeTree('/clickhouse/psim/{shard}/{database}/incident_report_states','{replica}')
PARTITION BY toYYYYMM(created_at) ORDER BY(tenant_id,created_at,incident_id)
TTL toDateTime(created_at)+INTERVAL 400 DAY DELETE)sql";
  // psim-arch: allow-sql (validated deployment identifier)
  const auto mv = "CREATE MATERIALIZED VIEW IF NOT EXISTS incident_report_mv ON CLUSTER " + cluster + R"sql(
TO incident_report_states AS SELECT tenant_id,incident_id,created_at,
argMaxState(tuple(site_id,zone_id,type_id,assignee_id,priority,state,resolution,ack_ms,resolved_ms,escalation_level,if(kind=1,version,toUInt64(0))),if(kind=1,version,toUInt64(0))) AS incident,
argMaxState(tuple(ack_target_ms,resolve_target_ms),if(kind=2,version,toUInt64(0))) AS sla
FROM incident_facts GROUP BY tenant_id,incident_id,created_at)sql";
  // psim-arch: allow-sql (validated deployment identifier)
  const auto current = "CREATE VIEW IF NOT EXISTS incident_report_current ON CLUSTER " + cluster + R"sql( AS
SELECT tenant_id,incident_id,created_at,i.1 AS site_id,i.2 AS zone_id,i.3 AS type_id,i.4 AS assignee_id,
i.5 AS priority,i.6 AS state,i.7 AS resolution,i.8 AS ack_ms,i.9 AS resolved_ms,i.10 AS escalation_level,
i.11 AS version,s.1 AS ack_target_ms,s.2 AS resolve_target_ms FROM
(SELECT tenant_id,incident_id,created_at,argMaxMerge(incident) AS i,argMaxMerge(sla) AS s
FROM incident_report_states GROUP BY tenant_id,incident_id,created_at) WHERE i.11>0)sql";
  return {.version = 1, .name = "incident reports", .expand = {raw, states, mv, current}};
}
}  // namespace psim::projection
