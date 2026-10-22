#include "state.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/ingest/v1/state.pb.h>
#include <psim/processing/v1/event.pb.h>

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

#include "psim/api/v1/feed.pb.h"
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
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::api_gateway {
namespace p = platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Param = p::pg::Parameter;
using Time = google::protobuf::util::TimeUtil;

namespace {
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
        result.push_back({record->envelope().tenant_id(), "observation", source,
                          Time::ToString(observation.observed_at()), record->session_id(),
                          observation.cursor().source_seq(), std::move(body)});
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

asio::awaitable<p::Result<void>> StateProjection::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  co_return co_await p::pg::Migrator("gateway_state")
      .apply(&**lease,
             {{.version = 1,
               .name = "temporary operational snapshots",
               .expand = {"CREATE TABLE gateway_state.resources(replica text NOT NULL,tenant uuid NOT NULL,kind text "
                          "NOT NULL,id uuid NOT NULL,version bigint NOT NULL,confirmed timestamptz,session text NOT "
                          "NULL,body jsonb NOT NULL,PRIMARY KEY(replica,tenant,kind,id))",
                          "CREATE TABLE gateway_state.positions(replica text NOT NULL,topic text NOT NULL,partition "
                          "integer NOT NULL,next_offset bigint NOT NULL,PRIMARY KEY(replica,topic,partition))"}}});
}

asio::awaitable<p::Result<void>> StateProjection::apply(const p::kafka::Message* message) {
  auto parsed = updates(*message);
  if (!parsed) {
    co_return std::unexpected(parsed.error());
  }
  co_return co_await p::pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [this, &message, &parsed](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        for (const auto& update : *parsed) {
          auto stored = co_await tx->connection().query(
              {.sql = "INSERT INTO gateway_state.resources "
                      "VALUES($1,$2::uuid,$3,$4::uuid,$5,NULLIF($6,'')::timestamptz,$7,$8::jsonb) ON "
                      "CONFLICT(replica,tenant,kind,id) DO UPDATE SET "
                      "version=EXCLUDED.version,confirmed=EXCLUDED.confirmed,session=EXCLUDED.session,body=CASE WHEN "
                      "EXCLUDED.kind='observation' THEN gateway_state.resources.body || EXCLUDED.body ELSE "
                      "EXCLUDED.body END WHERE (EXCLUDED.kind<>'observation' AND "
                      "gateway_state.resources.version<EXCLUDED.version) OR (EXCLUDED.kind='observation' AND "
                      "((gateway_state.resources.confirmed<EXCLUDED.confirmed AND "
                      "((gateway_state.resources.body->>'sourceEpoch') IS DISTINCT FROM "
                      "(EXCLUDED.body->>'sourceEpoch') OR gateway_state.resources.version<=EXCLUDED.version)) OR "
                      "(gateway_state.resources.confirmed=EXCLUDED.confirmed AND "
                      "gateway_state.resources.version<EXCLUDED.version)))",
               .parameters = {Param::text(replica_), Param::text(update.tenant), Param::text(update.kind),
                              Param::text(update.id), Param::integer(static_cast<std::int64_t>(update.version)),
                              Param::text(update.time), Param::text(update.session), Param::text(update.body.dump())}});
          if (!stored) {
            co_return std::unexpected(stored.error());
          }
        }
        auto positioned = co_await tx->connection().query(
            {.sql = "INSERT INTO gateway_state.positions VALUES($1,$2,$3,$4) ON CONFLICT(replica,topic,partition) DO "
                    "UPDATE SET next_offset=GREATEST(gateway_state.positions.next_offset,EXCLUDED.next_offset)",
             .parameters = {Param::text(replica_), Param::text(std::string(message->topic())),
                            Param::integer(message->partition()), Param::integer(message->offset() + 1)}});
        if (!positioned) {
          co_return std::unexpected(positioned.error());
        }
        co_return p::Result<void>{};
      });
}

asio::awaitable<p::Result<StateSnapshot>> StateProjection::snapshot(p::security::Principal principal,
                                                                    std::string location, std::set<std::string> sites) {
  struct Stored {
    std::map<std::string, Json> locations, devices, connectors, observations, connections;
    std::vector<incident::v1::Incident> incidents;
    StateSnapshot result;
  };

  auto stored = co_await p::pg::in_transaction<Stored>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [this, &principal, &sites](p::pg::Transaction* tx) -> asio::awaitable<p::Result<Stored>> {
        Stored data;
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT kind,id::text,body::text,COALESCE(to_char(confirmed AT TIME ZONE "
                    "'UTC','YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"'),'') FROM gateway_state.resources WHERE replica=$1 AND "
                    "tenant=$2::uuid ORDER BY kind,id LIMIT 100001",
             .parameters = {Param::text(replica_), Param::text(principal.tenant)}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        if (rows->size() > 100000) {
          co_return p::fail(p::ErrorCode::kCommonRateLimited, "snapshot resource limit reached");
        }
        for (std::size_t i = 0; i < rows->size(); ++i) {
          const auto kind = rows->value(i, 0).value_or("");
          const auto id = std::string(rows->value(i, 1).value_or(""));
          auto body = Json::parse(rows->value(i, 2).value_or(""));
          if (kind == "location") {
            data.locations[id] = std::move(body);
          } else if (kind == "device") {
            data.devices[id] = std::move(body);
          } else if (kind == "connector") {
            data.connectors[id] = std::move(body);
          } else if (kind == "observation") {
            body["confirmedAt"] = rows->value(i, 3).value_or("");
            data.observations[id] = std::move(body);
          } else if (kind == "connection") {
            data.connections[id] = std::move(body);
          }
        }
        auto positions = co_await tx->connection().query(
            {.sql = "SELECT topic,partition,next_offset FROM gateway_state.positions WHERE replica=$1 UNION ALL SELECT "
                    "'psim.incidents.events.v1',partition,next_offset FROM access.feed_offsets WHERE replica=$1",
             .parameters = {Param::text(replica_)}});
        if (!positions) {
          co_return std::unexpected(positions.error());
        }
        for (std::size_t i = 0; i < positions->size(); ++i) {
          data.result.offsets[std::string(positions->value(i, 0).value_or("")) + ":"
                              + std::to_string(*positions->integer(i, 1))] = *positions->integer(i, 2);
        }
        auto incidents = co_await tx->connection().query(
            {.sql = "SELECT body::text FROM access.incident_feed WHERE replica=$1 AND tenant_id=$2::uuid LIMIT 10001",
             .parameters = {Param::text(replica_), Param::text(principal.tenant)}});
        if (!incidents) {
          co_return std::unexpected(incidents.error());
        }
        if (incidents->size() > 10000) {
          co_return p::fail(p::ErrorCode::kCommonRateLimited, "snapshot incident limit reached");
        }
        for (std::size_t i = 0; i < incidents->size(); ++i) {
          incident::v1::Incident incident;
          if (!google::protobuf::util::JsonStringToMessage(std::string(incidents->value(i, 0).value_or("")), &incident)
                   .ok()) {
            co_return p::fail(p::ErrorCode::kCommonInternal, "invalid projected incident");
          }
          if ((sites.empty() || sites.contains(incident.site_id()))
              && incident.state() != incident::v1::INCIDENT_STATE_CLOSED
              && p::authz::scoped(principal, {principal.tenant, incident.site_id(), incident.zone_id()})) {
            data.incidents.push_back(std::move(incident));
          }
        }
        co_return data;
      },
      p::pg::Isolation::kRepeatableRead);
  if (!stored) {
    co_return std::unexpected(stored.error());
  }
  auto& result = stored->result;
  const auto now = Time::GetCurrentTime();
  *result.location.mutable_snapshot_at() = now;
  *result.operators.mutable_snapshot_at() = now;
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
    for (const auto& [user, count] : loads) {
      auto* load = result.operators.add_operators();
      load->set_user_id(user);
      load->set_assigned_open(count.first);
      load->set_escalated(count.second);
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
      if (leases_) {
        auto found =
            co_await p::redis::SessionRegistry(leases_, {"connector-gateway", principal.tenant}).lookup(connector);
        if (found) {
          registry_ok = true;
          session = std::move(*found);
        }
      }
      sessions[connector] = std::move(session);
      auto* status = result.location.add_connectors();
      status->set_connector_id(connector);
      status->set_quality(api::v1::DATA_QUALITY_UNKNOWN);
      status->set_connection(registry_ok ? "offline" : "unknown");
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
      if (!stored->observations.contains(source_id)) {
        missing = true;
        continue;
      }
      const auto& observation = stored->observations.at(source_id);
      google::protobuf::Timestamp confirmed;
      if (!Time::FromString(observation.value("confirmedAt", ""), &confirmed)) {
        missing = true;
        continue;
      }
      if (oldest.seconds() == 0 || Time::TimestampToMilliseconds(confirmed) < Time::TimestampToMilliseconds(oldest)) {
        oldest = confirmed;
      }
      const bool current = sessions[connector]
                           && observation.value("gatewaySessionId", "") == sessions[connector]->owner
                           && now.seconds() - confirmed.seconds() <= 60 && confirmed.seconds() <= now.seconds() + 30
                           && state == catalog::v1::RESOURCE_STATE_ACTIVE;
      stale = stale || !current;
      if (Time::TimestampToMilliseconds(confirmed) > Time::TimestampToMilliseconds(newest)) {
        newest = confirmed;
        device->set_last_type(observation.value("type", ""));
        *device->mutable_last_changed_at() = confirmed;
        common::v1::EventClass event_class{};
        (void)common::v1::EventClass_Parse(observation.value("eventClass", "EVENT_CLASS_UNSPECIFIED"), &event_class);
        device->set_state(event_class);
      }
      google::protobuf::Timestamp availability_confirmed;
      const bool availability_current =
          current && observation.value("availabilitySessionId", "") == sessions[connector]->owner
          && Time::FromString(observation.value("availabilityConfirmedAt", ""), &availability_confirmed)
          && now.seconds() - availability_confirmed.seconds() <= 60
          && availability_confirmed.seconds() <= now.seconds() + 30;
      if (availability_current && observation.contains("availability")) {
        if (observation.at("availability") == "DEVICE_AVAILABILITY_ONLINE") {
          ++online_sources;
        }
        if (observation.at("availability") == "DEVICE_AVAILABILITY_OFFLINE") {
          offline_source = true;
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
    if (device->quality() == api::v1::DATA_QUALITY_ACTUAL) {
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
}  // namespace psim::api_gateway
