#include "console_store.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/config/v1/config.pb.h>
#include <psim/incident/v1/events.pb.h>
#include <psim/ingest/v1/state.pb.h>
#include <psim/processing/v1/event.pb.h>
#include <psim/response/v1/events.pb.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "console_transaction.hpp"
#include "psim/api/v1/feed.pb.h"
#include "psim/catalog/v1/maintenance.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/connector/v1/connector.pb.h"
#include "psim/incident/v1/incident.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/redis/patterns.hpp"
#include "psim/platform/redis/serde.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::projection {
namespace p = platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Param = p::pg::Parameter;
using Time = google::protobuf::util::TimeUtil;

namespace {
std::uint64_t source_epoch(const Json& value) {
  const auto epoch = value.value("sourceEpoch", Json(0));
  return epoch.is_string() ? std::stoull(epoch.get<std::string>()) : epoch.get<std::uint64_t>();
}

bool report_covers(const Json& report, const Json& event) {
  if (source_epoch(report) != source_epoch(event)) {
    return source_epoch(report) > source_epoch(event);
  }
  return std::stoull(report.at("sequence").get<std::string>()) >= std::stoull(event.at("sequence").get<std::string>());
}

int state_rank(common::v1::EventClass value) {
  if (value == common::v1::EVENT_CLASS_ALARM) {
    return 4;
  }
  if (value == common::v1::EVENT_CLASS_FAULT) {
    return 3;
  }
  if (value == common::v1::EVENT_CLASS_WARNING) {
    return 2;
  }
  if (value == common::v1::EVENT_CLASS_STATE || value == common::v1::EVENT_CLASS_RESTORE) {
    return 1;
  }
  return 0;
}

struct Update {
  std::string tenant, kind, id, time, session;
  std::uint64_t version;
  Json body;
};

p::Result<std::vector<Update>> updates(const p::kafka::Message& message) {
  std::vector<Update> result;
  if (message.topic() == "psim.catalog.v1") {
    auto record = p::kafka::ProtobufSerde<catalog::v1::CatalogRecord>(1).deserialize(message.value());
    if (!record) {
      return std::unexpected(record.error());
    }
    std::string kind;
    std::string id;
    std::uint64_t version = 0;
    const google::protobuf::Message* body = nullptr;
    if (record->has_location()) {
      kind = "location";
      id = record->location().location_id();
      version = record->location().version();
      body = &record->location();
    } else if (record->has_device()) {
      kind = "device";
      id = record->device().device_id();
      version = record->device().version();
      body = &record->device();
    } else if (record->has_connector()) {
      kind = "connector";
      id = record->connector().connector_id();
      version = record->connector().version();
      body = &record->connector();
    }
    if (body) {
      result.push_back(
          {record->envelope().tenant_id(), kind, id, "", "", version, Json::parse(p::stream::json(*body))});
    }
  } else if (message.topic() == "psim.incidents.events.v1") {
    auto record = p::kafka::ProtobufSerde<incident::v1::IncidentEventRecord>(1).deserialize(message.value());
    if (!record) {
      return std::unexpected(record.error());
    }
    const auto* payload = incident::v1::IncidentEventRecord::GetReflection()->GetOneofFieldDescriptor(
        *record, incident::v1::IncidentEventRecord::GetDescriptor()->FindOneofByName("payload"));
    if (!payload) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "incident snapshot required");
    }
    const auto& change = incident::v1::IncidentEventRecord::GetReflection()->GetMessage(*record, payload);
    const auto* field = change.GetDescriptor()->FindFieldByName("incident");
    if (!field) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "incident state required");
    }
    incident::v1::Incident value;
    value.CopyFrom(change.GetReflection()->GetMessage(change, field));
    result.push_back({record->envelope().tenant_id(), "incident", value.incident_id(), "", "", value.version(),
                      Json::parse(p::stream::json(value))});
  } else if (message.topic() == "psim.config.v1") {
    auto record = p::kafka::ProtobufSerde<config::v1::ConfigRecord>(1).deserialize(message.value());
    if (!record) {
      return std::unexpected(record.error());
    }
    if (record->has_maintenance_window()) {
      const auto& value = record->maintenance_window();
      result.push_back({record->envelope().tenant_id(), "maintenance", value.maintenance_id(), "", "", value.version(),
                        Json::parse(p::stream::json(value))});
    }
  } else if (message.topic() == "psim.response.events.v1") {
    auto record = p::kafka::ProtobufSerde<response::v1::ResponseEventRecord>(1).deserialize(message.value());
    if (!record) {
      return std::unexpected(record.error());
    }
    if (record->has_site_duty_changed()) {
      const auto& value = record->site_duty_changed();
      result.push_back({record->envelope().tenant_id(), "duty", value.site_id(), "", "", value.version(),
                        Json::parse(p::stream::json(value))});
    }
  } else if (message.topic() == "psim.events.normalized.v1") {
    auto record = p::kafka::ProtobufSerde<processing::v1::EventRecord>(1).deserialize(message.value());
    if (!record) {
      return std::unexpected(record.error());
    }
    const auto& event = record->event();
    if (record->has_event() && event.event_class() != common::v1::EVENT_CLASS_INFO
        && event.event_class() != common::v1::EVENT_CLASS_UNSPECIFIED && event.live_observation()
        && event.has_state_observed_at() && Time::IsTimestampValid(event.state_observed_at())
        && event.state_observed_at().seconds() > 0) {
      auto body = Json::parse(p::stream::json(event));
      body["sequence"] = std::to_string(record->envelope().sequence());
      result.push_back({record->envelope().tenant_id(), "observation", event.source_id(),
                        Time::ToString(event.state_observed_at()), event.gateway_session_id(),
                        record->envelope().sequence(), std::move(body)});
    }
  } else if (message.topic() == "psim.ingest.state.v1") {
    auto record = p::kafka::ProtobufSerde<ingest::v1::GatewayStateRecord>(1).deserialize(message.value());
    if (!record) {
      return std::unexpected(record.error());
    }
    if (record->has_connection()) {
      result.push_back({record->envelope().tenant_id(), "connection", record->connector_id(),
                        Time::ToString(record->envelope().occurred_at()), record->session_id(),
                        record->envelope().sequence(), Json::parse(p::stream::json(*record))});
    }
    if (record->has_state_report()) {
      for (const auto& observation : record->state_report().observations()) {
        const auto source = p::stream::uuid5("728d47ed-1c69-5809-a2df-6e8b98e48ca6",
                                             {record->connector_id(), observation.cursor().source_ref()});
        Json body{{"sourceId", source},
                  {"connectorId", record->connector_id()},
                  {"gatewaySessionId", record->session_id()},
                  {"sourceEpoch", observation.cursor().source_epoch()},
                  {"sequence", std::to_string(observation.cursor().source_seq())},
                  {"availability", connector::v1::DeviceAvailability_Name(observation.availability())},
                  {"availabilityConfirmedAt", Time::ToString(observation.observed_at())},
                  {"availabilitySessionId", record->session_id()},
                  {"values", observation.values()}};
        result.push_back({record->envelope().tenant_id(), "reported", source, Time::ToString(observation.observed_at()),
                          record->session_id(), observation.cursor().source_seq(), std::move(body)});
      }
    }
  }
  for (const auto& update : result) {
    if (!p::stream::uuid(update.tenant) || !p::stream::uuid(update.id)
        || update.version > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid state identity or version");
    }
  }
  return result;
}
}  // namespace

asio::awaitable<p::Result<void>> ConsoleStore::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  co_return co_await p::pg::Migrator("console").apply(
      &**lease,
      {{.version = 1,
        .name = "console read models",
        .expand =
            {("CREATE TABLE console.resources(replica text NOT NULL,tenant uuid NOT NULL,kind text "
              "NOT NULL,id uuid NOT NULL,version bigint NOT NULL,confirmed timestamptz,session text NOT "
              "NULL,body jsonb NOT NULL,PRIMARY KEY(replica,tenant,kind,id))"),
             ("CREATE TABLE console.positions(replica text NOT NULL,topic text NOT NULL,partition "
              "integer NOT NULL,next_offset bigint NOT NULL,PRIMARY KEY(replica,topic,partition))"),
             "CREATE TABLE console.generations(id text PRIMARY KEY,revision bigint NOT NULL DEFAULT 0)",
             "INSERT INTO console.generations(id) VALUES('initial')",
             "CREATE TABLE console.active(singleton boolean PRIMARY KEY CHECK(singleton),generation text NOT NULL)",
             "INSERT INTO console.active VALUES(true,'initial')",
             ("CREATE INDEX console_resource_scope ON "
              "console.resources(replica,tenant,kind,(body->>'siteId'),(body->>'zoneId'))")}},
       {.version = 2,
        .name = "historical console state",
        .expand = {
            ("CREATE TABLE console.history(sequence bigserial PRIMARY KEY,tenant uuid NOT NULL,kind text NOT NULL,id "
             "uuid NOT NULL,captured timestamptz NOT NULL,confirmed timestamptz,body jsonb NOT NULL)"),
            "CREATE INDEX console_history_lookup ON console.history(tenant,kind,id,captured DESC,sequence DESC)",
            "CREATE INDEX console_history_retention ON console.history(captured)",
            ("INSERT INTO console.history(tenant,kind,id,captured,confirmed,body) SELECT "
             "tenant,kind,id,clock_timestamp(),confirmed,body FROM console.resources WHERE replica=(SELECT generation "
             "FROM console.active)"),
            ("CREATE FUNCTION console.capture_history() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
             "IF NEW.replica=(SELECT generation FROM console.active) THEN "
             "INSERT INTO console.history(tenant,kind,id,captured,confirmed,body) "
             "VALUES(NEW.tenant,NEW.kind,NEW.id,clock_timestamp(),NEW.confirmed,NEW.body); END IF; RETURN NEW; END $$"),
            ("CREATE TRIGGER console_resource_history AFTER INSERT OR UPDATE ON console.resources FOR EACH ROW EXECUTE "
             "FUNCTION console.capture_history()")}}});
}

asio::awaitable<p::Result<void>> ConsoleStore::apply(const p::kafka::Message* message) {
  auto parsed = updates(*message);
  if (!parsed) {
    co_return std::unexpected(parsed.error());
  }
  co_return co_await console_transaction<void>(
      pool_, writer_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [this, &message, &parsed](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        for (const auto& update : *parsed) {
          auto stored = co_await tx->connection().query(
              {.sql =
                   "INSERT INTO console.resources "
                   "VALUES($1,$2::uuid,$3,$4::uuid,$5,NULLIF($6,'')::timestamptz,$7,$8::jsonb) ON "
                   "CONFLICT(replica,tenant,kind,id) DO UPDATE SET "
                   "version=EXCLUDED.version,confirmed=EXCLUDED.confirmed,session=EXCLUDED.session,body=EXCLUDED.body "
                   "WHERE (EXCLUDED.kind NOT IN ('observation','reported','connection') AND "
                   "console.resources.version<EXCLUDED.version) "
                   "OR (EXCLUDED.kind='connection' AND (console.resources.confirmed<EXCLUDED.confirmed OR "
                   "(console.resources.confirmed=EXCLUDED.confirmed AND console.resources.version<EXCLUDED.version))) "
                   "OR (EXCLUDED.kind IN ('observation','reported') AND "
                   "(EXCLUDED.body->>'sourceEpoch')::numeric >= (console.resources.body->>'sourceEpoch')::numeric AND "
                   "((console.resources.confirmed<EXCLUDED.confirmed AND "
                   "((EXCLUDED.body->>'sourceEpoch')::numeric > (console.resources.body->>'sourceEpoch')::numeric OR "
                   "console.resources.version<=EXCLUDED.version)) OR "
                   "(console.resources.confirmed=EXCLUDED.confirmed AND ((EXCLUDED.body->>'sourceEpoch')::numeric > "
                   "(console.resources.body->>'sourceEpoch')::numeric OR "
                   "console.resources.version<EXCLUDED.version))))",
               .parameters = {Param::text(replica_), Param::text(update.tenant), Param::text(update.kind),
                              Param::text(update.id), Param::integer(static_cast<std::int64_t>(update.version)),
                              Param::text(update.time), Param::text(update.session), Param::text(update.body.dump())}});
          if (!stored) {
            co_return std::unexpected(stored.error());
          }
        }
        auto positioned = co_await tx->connection().query(
            {.sql = "INSERT INTO console.positions VALUES($1,$2,$3,$4) ON CONFLICT(replica,topic,partition) DO "
                    "UPDATE SET next_offset=GREATEST(console.positions.next_offset,EXCLUDED.next_offset)",
             .parameters = {Param::text(replica_), Param::text(std::string(message->topic())),
                            Param::integer(message->partition()), Param::integer(message->offset() + 1)}});
        if (!positioned) {
          co_return std::unexpected(positioned.error());
        }
        auto revision =
            co_await tx->connection().query({.sql = "UPDATE console.generations SET revision=revision+1 WHERE id=$1",
                                             .parameters = {Param::text(replica_)}});
        if (!revision) {
          co_return std::unexpected(revision.error());
        }
        co_return p::Result<void>{};
      });
}

asio::awaitable<p::Result<StateSnapshot>> ConsoleStore::snapshot(p::security::Principal principal, std::string location,
                                                                 std::set<std::string> sites,
                                                                 std::optional<std::int64_t> at) {
  struct Stored {
    std::map<std::string, Json> locations, devices, connectors, observations, reports, connections, maintenance, duties;
    std::vector<incident::v1::Incident> incidents;
    StateSnapshot result;
  };

  auto stored = co_await p::pg::in_transaction<Stored>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [this, &principal, &sites, &at](p::pg::Transaction* tx) -> asio::awaitable<p::Result<Stored>> {
        Stored data;
        auto current = co_await tx->connection().query({.sql = "SELECT a.generation,g.revision FROM console.active a "
                                                               "JOIN console.generations g ON g.id=a.generation"});
        if (!current || current->size() != 1) {
          co_return p::fail(p::ErrorCode::kCommonUnavailable, "console generation unavailable");
        }
        const auto generation = std::string(current->value(0, 0).value_or(""));
        const auto revision = std::to_string(*current->integer(0, 1));
        data.result.generation = generation;
        Json material;
        if (leases_ && !at) {
          auto cached = co_await p::redis::Cache(leases_, {"projection-service", principal.tenant})
                            .get(generation + ":" + revision);
          if (cached && *cached) {
            material = Json::parse(**cached, nullptr, false);
          }
        }
        if (material.is_array() && hits_) {
          hits_->Increment();
        }
        if (!material.is_array()) {
          if (misses_) {
            misses_->Increment();
          }
          auto rows = at ? co_await tx->connection().query(
                               {.sql = "SELECT kind,id::text,body::text,COALESCE(to_char(confirmed AT TIME ZONE "
                                       "'UTC','YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"'),'') FROM "
                                       "(SELECT DISTINCT ON(kind,id) kind,id,body,confirmed FROM console.history WHERE "
                                       "tenant=$1::uuid "
                                       "AND captured<=to_timestamp($2::double precision/1000) ORDER BY "
                                       "kind,id,captured DESC,sequence DESC) state ORDER BY kind,id LIMIT 100001",
                                .parameters = {Param::text(principal.tenant), Param::integer(*at)}})
                         : co_await tx->connection().query(
                               {.sql = "SELECT kind,id::text,body::text,COALESCE(to_char(confirmed AT TIME ZONE "
                                       "'UTC','YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"'),'') FROM console.resources WHERE "
                                       "replica=$1 AND "
                                       "tenant=$2::uuid ORDER BY kind,id LIMIT 100001",
                                .parameters = {Param::text(generation), Param::text(principal.tenant)}});
          if (!rows) {
            co_return std::unexpected(rows.error());
          }
          if (rows->size() > 100000) {
            co_return p::fail(p::ErrorCode::kCommonRateLimited, "snapshot resource limit reached");
          }
          material = Json::array();
          for (std::size_t i = 0; i < rows->size(); ++i) {
            material.push_back({{"kind", rows->value(i, 0).value_or("")},
                                {"id", rows->value(i, 1).value_or("")},
                                {"body", Json::parse(rows->value(i, 2).value_or(""))},
                                {"confirmedAt", rows->value(i, 3).value_or("")}});
          }
          if (leases_ && !at) {
            auto bytes = material.dump();
            if (bytes.size() <= p::redis::kMaxPayloadBytes) {
              auto cached = co_await p::redis::Cache(leases_, {"projection-service", principal.tenant})
                                .put(generation + ":" + revision, std::move(bytes), std::chrono::seconds(5));
              (void)cached;
            }
          }
        }
        for (const auto& entry : material) {
          const auto kind = entry.at("kind").get<std::string>();
          const auto id = entry.at("id").get<std::string>();
          auto body = entry.at("body");
          if (kind == "incident") {
            incident::v1::Incident incident;
            if (!google::protobuf::util::JsonStringToMessage(body.dump(), &incident).ok()) {
              co_return p::fail(p::ErrorCode::kCommonInternal, "invalid projected incident");
            }
            if ((sites.empty() || sites.contains(incident.site_id()))
                && incident.state() != incident::v1::INCIDENT_STATE_CLOSED
                && p::authz::scoped(principal, {principal.tenant, incident.site_id(), incident.zone_id()})) {
              data.incidents.push_back(std::move(incident));
              if (data.incidents.size() > 10000) {
                co_return p::fail(p::ErrorCode::kCommonRateLimited, "incident snapshot limit reached");
              }
            }
          } else if (kind == "maintenance") {
            data.maintenance[id] = std::move(body);
          } else if (kind == "duty") {
            data.duties[id] = std::move(body);
          } else if (kind == "location") {
            data.locations[id] = std::move(body);
          } else if (kind == "device") {
            data.devices[id] = std::move(body);
          } else if (kind == "connector") {
            data.connectors[id] = std::move(body);
          } else if (kind == "observation") {
            body["confirmedAt"] = entry.at("confirmedAt");
            data.observations[id] = std::move(body);
          } else if (kind == "reported") {
            body["confirmedAt"] = entry.at("confirmedAt");
            data.reports[id] = std::move(body);
          } else if (kind == "connection") {
            data.connections[id] = std::move(body);
          }
        }
        if (at) {
          auto first =
              co_await tx->connection().query({.sql = "SELECT (EXTRACT(EPOCH FROM MIN(captured))*1000)::bigint::text "
                                                      "FROM console.history WHERE tenant=$1::uuid",
                                               .parameters = {Param::text(principal.tenant)}});
          if (!first) {
            co_return std::unexpected(first.error());
          }
          if (!first->value(0, 0) || std::stoll(std::string(*first->value(0, 0))) > *at) {
            co_return p::fail(p::ErrorCode::kCommonNotFound, "historical state unavailable for this time");
          }
          *data.result.history.mutable_available_from() =
              Time::MillisecondsToTimestamp(std::stoll(std::string(*first->value(0, 0))));
          data.result.history.set_historical(true);
        }
        auto positions = co_await tx->connection().query(
            {.sql = "SELECT topic,partition,next_offset FROM console.positions WHERE replica=$1",
             .parameters = {Param::text(generation)}});
        if (!positions) {
          co_return std::unexpected(positions.error());
        }
        for (std::size_t i = 0; i < positions->size(); ++i) {
          data.result.offsets[std::string(positions->value(i, 0).value_or("")) + ":"
                              + std::to_string(*positions->integer(i, 1))] = *positions->integer(i, 2);
        }
        co_return data;
      },
      p::pg::Isolation::kRepeatableRead);
  if (!stored) {
    co_return std::unexpected(stored.error());
  }
  auto& result = stored->result;
  const auto now = at ? Time::MillisecondsToTimestamp(*at) : Time::GetCurrentTime();
  *result.location.mutable_snapshot_at() = now;
  *result.operators.mutable_snapshot_at() = now;
  *result.feed.mutable_snapshot_at() = now;
  if (at) {
    for (const auto& [id, body] : stored->locations) {
      if ((sites.empty() || sites.contains(body.value("siteId", "")))
          && p::authz::scoped(principal, {principal.tenant, body.value("siteId", ""), id})) {
        catalog::v1::Location entry;
        if (google::protobuf::util::JsonStringToMessage(body.dump(), &entry).ok()) {
          *result.history.add_catalog_locations() = entry;
        }
      }
    }
    for (const auto& [id, body] : stored->devices) {
      (void)id;
      if ((sites.empty() || sites.contains(body.value("siteId", "")))
          && p::authz::scoped(principal, {principal.tenant, body.value("siteId", ""), body.value("zoneId", "")})) {
        catalog::v1::Device entry;
        if (google::protobuf::util::JsonStringToMessage(body.dump(), &entry).ok()) {
          *result.history.add_catalog_devices() = entry;
        }
      }
    }
  }
  for (const auto& incident : stored->incidents) {
    *result.feed.add_incidents() = incident;
  }
  if (location.empty()) {
    std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> loads;
    for (const auto& incident : stored->incidents) {
      if (!incident.assignee_id().empty()) {
        ++loads[incident.assignee_id()].first;
        if (incident.escalation_level() > 0) {
          ++loads[incident.assignee_id()].second;
        }
      }
    }
    for (const auto& [id, duty] : stored->duties) {
      if ((sites.empty() || sites.contains(id)) && p::authz::scoped(principal, {principal.tenant, id})) {
        for (const auto* field : {"userId", "fallbackUserId"}) {
          const auto user = duty.value(field, "");
          if (!user.empty()) {
            loads.try_emplace(user);
          }
        }
      }
    }
    for (const auto& [user, count] : loads) {
      auto* load = result.operators.add_operators();
      load->set_user_id(user);
      load->set_assigned_open(count.first);
      load->set_escalated(count.second);
      for (const auto& [id, duty] : stored->duties) {
        if ((sites.empty() || sites.contains(id)) && p::authz::scoped(principal, {principal.tenant, id})) {
          if (duty.value("userId", "") == user) {
            load->add_duty_site_ids(id);
          }
          if (duty.value("fallbackUserId", "") == user) {
            load->add_fallback_site_ids(id);
          }
        }
      }
    }
    co_return std::move(result);
  }
  if (!stored->locations.contains(location)) {
    co_return p::fail(p::ErrorCode::kCommonNotFound, "location not found");
  }
  const auto& root = stored->locations.at(location);
  if (!p::authz::scoped(principal, {principal.tenant, root.value("siteId", ""), location})) {
    co_return p::fail(p::ErrorCode::kAuthScopeDenied, "location outside access scope");
  }
  std::set<std::string> subtree{location};
  for (std::size_t pass = 0; pass < 5; ++pass) {
    for (const auto& [id, body] : stored->locations) {
      if (subtree.contains(body.value("parentId", ""))) {
        subtree.insert(id);
      }
    }
  }
  for (const auto& [id, duty] : stored->duties) {
    (void)id;
    const auto site = duty.value("siteId", "");
    if (p::authz::scoped(principal, {principal.tenant, site})
        && (root.value("siteId", "") == site || location == site)) {
      auto* value = result.location.add_site_duties();
      value->set_site_id(site);
      value->set_user_id(duty.value("userId", ""));
      value->set_fallback_user_id(duty.value("fallbackUserId", ""));
      const auto version = duty.at("version");
      value->set_version(version.is_string() ? std::stoull(version.get<std::string>()) : version.get<std::uint64_t>());
    }
  }
  for (const auto& [id, window] : stored->maintenance) {
    (void)id;
    catalog::v1::MaintenanceWindow value;
    if (!google::protobuf::util::JsonStringToMessage(window.dump(), &value).ok()) {
      co_return p::fail(p::ErrorCode::kCommonInternal, "invalid maintenance projection");
    }
    const auto maintenance_zone = value.has_device_id() && stored->devices.contains(value.device_id())
                                      ? stored->devices.at(value.device_id()).value("zoneId", "")
                                      : value.zone_id();
    if (value.state() != catalog::v1::MAINTENANCE_STATE_CANCELLED
        && value.state() != catalog::v1::MAINTENANCE_STATE_COMPLETED
        && Time::TimestampToMilliseconds(value.starts_at()) <= Time::TimestampToMilliseconds(now)
        && Time::TimestampToMilliseconds(now) < Time::TimestampToMilliseconds(value.ends_at())
        && p::authz::scoped(principal, {principal.tenant, value.site_id(), maintenance_zone})) {
      const bool belongs = subtree.contains(value.zone_id())
                           || (stored->devices.contains(value.device_id())
                               && subtree.contains(stored->devices.at(value.device_id()).value("zoneId", "")));
      if (belongs) {
        *result.location.add_maintenance_windows() = value;
      }
    }
  }
  std::map<std::string, std::optional<p::redis::Session>> sessions;
  std::map<std::string, api::v1::ZoneStatus*> zones;
  for (const auto& [id, body] : stored->locations) {
    if (subtree.contains(id) && body.value("kind", "") == "LOCATION_KIND_ZONE"
        && p::authz::scoped(principal, {principal.tenant, body.value("siteId", ""), id})) {
      auto* zone = result.location.add_zones();
      zone->set_zone_id(id);
      zone->set_quality(api::v1::DATA_QUALITY_UNKNOWN);
      catalog::v1::ResourceState state{};
      (void)catalog::v1::ResourceState_Parse(body.value("state", "RESOURCE_STATE_UNSPECIFIED"), &state);
      zone->set_resource_state(state);
      zones[id] = zone;
      for (const auto& incident : stored->incidents) {
        if (incident.zone_id() == id) {
          zone->set_open_incidents(zone->open_incidents() + 1);
        }
      }
    }
  }
  std::set<std::string> zone_seen;
  for (const auto& [id, body] : stored->devices) {
    const auto zone = body.value("zoneId", "");
    if (!zones.contains(zone)) {
      continue;
    }
    const auto connector = body.value("connectorId", "");
    if (!sessions.contains(connector)) {
      std::optional<p::redis::Session> session;
      bool registry_ok = false;
      if (leases_ && !at) {
        auto found =
            co_await p::redis::SessionRegistry(leases_, {"connector-gateway", principal.tenant}).lookup(connector);
        if (found) {
          registry_ok = true;
          session = std::move(*found);
        }
      }
      catalog::v1::ResourceState connector_state{};
      if (stored->connectors.contains(connector)) {
        (void)catalog::v1::ResourceState_Parse(
            stored->connectors.at(connector).value("state", "RESOURCE_STATE_UNSPECIFIED"), &connector_state);
      }
      bool reported_offline = false;
      if (session && stored->connections.contains(connector)) {
        const auto& connection = stored->connections.at(connector);
        google::protobuf::Timestamp confirmed;
        reported_offline =
            connection.value("sessionId", "") == session->owner && !connection.at("connection").value("online", false)
            && Time::FromString(connection.at("envelope").at("occurredAt").get<std::string>(), &confirmed)
            && now.seconds() - confirmed.seconds() <= 60 && confirmed.seconds() <= now.seconds() + 30;
      }
      if (reported_offline || connector_state != catalog::v1::RESOURCE_STATE_ACTIVE) {
        session.reset();
      }
      sessions[connector] = std::move(session);
      auto* status = result.location.add_connectors();
      status->set_resource_state(connector_state);
      status->set_connector_id(connector);
      status->set_quality(registry_ok ? api::v1::DATA_QUALITY_ACTUAL : api::v1::DATA_QUALITY_UNKNOWN);
      status->set_connection(registry_ok || reported_offline ? "offline" : "unknown");
      if (reported_offline) {
        status->set_quality(api::v1::DATA_QUALITY_ACTUAL);
      }
      if (sessions[connector]) {
        status->set_session_id(sessions[connector]->owner);
        status->set_connection("online");
        status->set_quality(api::v1::DATA_QUALITY_ACTUAL);
        *status->mutable_confirmed_at() = now;
      }
      if (!sessions[connector] && stored->connections.contains(connector)) {
        const auto& connection = stored->connections.at(connector);
        google::protobuf::Timestamp confirmed;
        if (Time::FromString(connection.at("envelope").at("occurredAt").get<std::string>(), &confirmed)) {
          *status->mutable_confirmed_at() = confirmed;
        }
      }
    }
    auto* device = result.location.add_devices();
    device->set_device_id(id);
    device->set_connector_id(connector);
    device->set_connection("unknown");
    catalog::v1::ResourceState state{};
    (void)catalog::v1::ResourceState_Parse(body.value("state", "RESOURCE_STATE_UNSPECIFIED"), &state);
    device->set_resource_state(state);
    bool missing = body.value("sources", Json::array()).empty();
    bool stale = false;
    std::size_t online_sources = 0;
    bool offline_source = false;
    google::protobuf::Timestamp oldest;
    google::protobuf::Timestamp newest;
    for (const auto& source : body.value("sources", Json::array())) {
      const auto source_id = source.value("sourceId", "");
      auto* source_status = device->add_sources();
      source_status->set_source_id(source_id);
      source_status->set_state_quality(api::v1::DATA_QUALITY_UNKNOWN);
      source_status->set_values_quality(api::v1::DATA_QUALITY_UNKNOWN);
      source_status->set_availability_quality(api::v1::DATA_QUALITY_UNKNOWN);
      source_status->set_availability("unknown");
      const bool has_event = stored->observations.contains(source_id);
      const bool has_report = stored->reports.contains(source_id);
      if (!has_event && !has_report) {
        missing = true;
        continue;
      }
      Json observation = has_event ? stored->observations.at(source_id) : stored->reports.at(source_id);
      const bool report_current =
          has_report
          && (!has_event || report_covers(stored->reports.at(source_id), stored->observations.at(source_id)));
      if (has_report) {
        const auto& report = stored->reports.at(source_id);
        if (report_current
            && (!has_event || source_epoch(report) > source_epoch(observation)
                || (source_epoch(report) == source_epoch(observation)
                    && report.at("confirmedAt").get<std::string>()
                           > observation.at("confirmedAt").get<std::string>()))) {
          observation["confirmedAt"] = report.at("confirmedAt");
          observation["gatewaySessionId"] = report.at("gatewaySessionId");
        }
        for (const auto* field : {"availability", "availabilityConfirmedAt", "availabilitySessionId", "values"}) {
          observation[field] = report.at(field);
        }
      }
      google::protobuf::Timestamp confirmed;
      if (!Time::FromString(observation.value("confirmedAt", ""), &confirmed)) {
        missing = true;
        continue;
      }
      if (oldest.seconds() == 0 || Time::TimestampToMilliseconds(confirmed) < Time::TimestampToMilliseconds(oldest)) {
        oldest = confirmed;
      }
      const auto epoch = source_epoch(observation);
      const bool current = sessions[connector]
                           && observation.value("gatewaySessionId", "") == sessions[connector]->owner
                           && now.seconds() - confirmed.seconds() <= 60 && confirmed.seconds() <= now.seconds() + 30
                           && state == catalog::v1::RESOURCE_STATE_ACTIVE;
      stale = stale || !current;
      auto field_quality = [&](const Json& value, google::protobuf::Timestamp* at) {
        if (!Time::FromString(value.value("confirmedAt", ""), at)) {
          return api::v1::DATA_QUALITY_UNKNOWN;
        }
        return source_epoch(value) == epoch && sessions[connector]
                       && value.value("gatewaySessionId", "") == sessions[connector]->owner
                       && now.seconds() - at->seconds() <= 60 && at->seconds() <= now.seconds() + 30
                       && state == catalog::v1::RESOURCE_STATE_ACTIVE
                   ? api::v1::DATA_QUALITY_ACTUAL
                   : api::v1::DATA_QUALITY_STALE;
      };
      if (has_event) {
        google::protobuf::Timestamp observed;
        const auto quality = field_quality(stored->observations.at(source_id), &observed);
        source_status->set_state_quality(quality);
        *source_status->mutable_state_confirmed_at() = observed;
        common::v1::EventClass event_class{};
        (void)common::v1::EventClass_Parse(
            stored->observations.at(source_id).value("eventClass", "EVENT_CLASS_UNSPECIFIED"), &event_class);
        source_status->set_state(event_class);
        if (state_rank(event_class) > state_rank(device->state())) {
          device->set_state(event_class);
        }
      }
      if (has_report) {
        google::protobuf::Timestamp observed;
        const auto quality = field_quality(stored->reports.at(source_id), &observed);
        source_status->set_values_quality(report_current ? quality : api::v1::DATA_QUALITY_STALE);
        *source_status->mutable_values_confirmed_at() = observed;
        for (const auto& [name, value] : stored->reports.at(source_id).at("values").items()) {
          (*source_status->mutable_values())[name] = value.get<std::string>();
        }
      }

      if (has_event) {
        google::protobuf::Timestamp changed;
        const auto& event = stored->observations.at(source_id);
        if (Time::FromString(event.value("confirmedAt", ""), &changed)
            && Time::TimestampToMilliseconds(changed) > Time::TimestampToMilliseconds(newest)) {
          newest = changed;
          device->set_last_type(event.value("type", ""));
          *device->mutable_last_changed_at() = changed;
        }
      }
      google::protobuf::Timestamp availability_confirmed;
      const bool availability_known =
          Time::FromString(observation.value("availabilityConfirmedAt", ""), &availability_confirmed);
      const bool availability_current = availability_known && current && report_current
                                        && source_epoch(stored->reports.at(source_id)) == epoch
                                        && observation.value("availabilitySessionId", "") == sessions[connector]->owner
                                        && now.seconds() - availability_confirmed.seconds() <= 60
                                        && availability_confirmed.seconds() <= now.seconds() + 30;
      if (has_report) {
        source_status->set_availability_quality(availability_current ? api::v1::DATA_QUALITY_ACTUAL
                                                                     : api::v1::DATA_QUALITY_STALE);
        *source_status->mutable_availability_confirmed_at() = availability_confirmed;
      }
      if (availability_current && observation.contains("availability")) {
        if (observation.at("availability") == "DEVICE_AVAILABILITY_ONLINE") {
          ++online_sources;
          source_status->set_availability("online");
        }
        if (observation.at("availability") == "DEVICE_AVAILABILITY_OFFLINE") {
          offline_source = true;
          source_status->set_availability("offline");
        }
      }
    }
    device->set_quality(api::v1::DATA_QUALITY_ACTUAL);
    if (stale) {
      device->set_quality(api::v1::DATA_QUALITY_STALE);
    }
    if (missing) {
      device->set_quality(api::v1::DATA_QUALITY_UNKNOWN);
    }
    if (oldest.seconds() > 0) {
      *device->mutable_confirmed_at() = oldest;
    }
    if (!body.value("sources", Json::array()).empty()) {
      if (offline_source) {
        device->set_connection("offline");
      } else if (online_sources == body.value("sources", Json::array()).size()) {
        device->set_connection("online");
        device->set_online(true);
      }
    }
    auto* summary = zones.at(zone);
    if (!zone_seen.contains(zone)) {
      summary->set_quality(device->quality());
      zone_seen.insert(zone);
    } else if (summary->quality() == api::v1::DATA_QUALITY_ACTUAL
               || device->quality() == api::v1::DATA_QUALITY_UNKNOWN) {
      summary->set_quality(device->quality());
    }
    if (device->has_confirmed_at()
        && (!summary->has_confirmed_at() || device->confirmed_at().seconds() < summary->confirmed_at().seconds())) {
      *summary->mutable_confirmed_at() = device->confirmed_at();
    }
    if (state_rank(device->state()) > state_rank(summary->state())) {
      summary->set_state(device->state());
    }
  }
  co_return std::move(result);
}
}  // namespace psim::projection
