#include "repository.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/api/v1/incidents.pb.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/config/v1/config.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "psim/common/v1/envelope.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/domain/incident/model.hpp"
#include "psim/incident/v1/events.pb.h"
#include "psim/incident/v1/incident.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/signal.pb.h"
#include "psim/response/v1/command.pb.h"
#include "psim/response/v1/events.pb.h"
#include "psim/response/v1/notification.pb.h"
#include "psim/response/v1/run.pb.h"

namespace psim::incident_service {
namespace pg = p::pg;
namespace d = domain::incident;
using Param = pg::Parameter;
using Time = google::protobuf::util::TimeUtil;
using Code = p::ErrorCode;

namespace {
Json json(const google::protobuf::Message& value) {
  return Json::parse(p::stream::json(value));
}

template <typename T>
p::Result<T> parse(const Json& input) {
  T value;
  if (!google::protobuf::util::JsonStringToMessage(input.dump(), &value).ok()) {
    return p::fail(Code::kCommonInvalidArgument, "invalid or unknown fields");
  }
  return value;
}

p::security::Principal system(std::string tenant) {
  return {.subject = "incident-service",
          .tenant = std::move(tenant),
          .expires_at = std::chrono::system_clock::time_point::max()};
}

void envelope(common::v1::Envelope* out, p::security::Principal principal, std::uint64_t version = 1) {
  out->set_message_id(p::stream::uuid7());
  out->set_tenant_id(principal.tenant);
  out->set_sequence(version);
  out->set_correlation_id(out->message_id());
  out->set_schema_version("1");
  *out->mutable_occurred_at() = Time::GetCurrentTime();
  *out->mutable_produced_at() = out->occurred_at();
  out->mutable_actor()->set_id(principal.subject);
  out->mutable_actor()->set_kind(principal.subject == "incident-service" ? common::v1::ACTOR_KIND_SERVICE
                                                                         : common::v1::ACTOR_KIND_USER);
}

asio::awaitable<p::Result<incident::v1::Incident>> load(pg::Transaction* tx, std::string tenant, std::string id) {
  auto rows = co_await tx->connection().query(
      {.sql = "SELECT body::text FROM incident.incidents WHERE tenant_id=$1::uuid AND incident_id=$2::uuid FOR UPDATE",
       .parameters = {Param::text(tenant), Param::text(id)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0) {
    co_return p::fail(Code::kCommonNotFound, "incident not found");
  }
  co_return parse<incident::v1::Incident>(Json::parse(rows->value(0, 0).value_or("")));
}

Code failure(d::Failure value) {
  switch (value) {
    case d::Failure::kTransition:
      return Code::kIncidentInvalidStateTransition;
    case d::Failure::kAssignee:
      return Code::kIncidentAssigneeInvalid;
    case d::Failure::kForbidden:
      return Code::kIncidentResolutionForbidden;
    case d::Failure::kBlocking:
      return Code::kIncidentMandatoryStepsPending;
    case d::Failure::kRunVersion:
      return Code::kIncidentRunVersionStale;
    case d::Failure::kArgument:
      return Code::kCommonInvalidArgument;
  }
  return Code::kCommonInternal;
}
}  // namespace

boost::asio::awaitable<p::Result<void>> Repository::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto messages = co_await messages_.initialize(&**lease);
  if (!messages) {
    co_return messages;
  }
  co_return co_await pg::Migrator("incident")
      .apply(
          &**lease,
          {{.version = 1,
            .name = "minimal incident aggregate",
            .expand = {("CREATE TABLE incident.incidents (tenant_id uuid NOT NULL, incident_id uuid PRIMARY KEY, "
                        "grouping_key text NOT NULL, group_until_ns bigint NOT NULL, body jsonb NOT NULL)"),
                       "CREATE INDEX incident_group ON incident.incidents(tenant_id, grouping_key, group_until_ns)"}},
           {.version = 2,
            .name = "incident lifecycle and configuration",
            .expand =
                {"ALTER TABLE incident.incidents ADD COLUMN grouping_active boolean NOT NULL DEFAULT false",
                 (("UPDATE incident.incidents SET grouping_active=true WHERE incident_id IN (SELECT DISTINCT ON "
                   "(tenant_id,grouping_key) incident_id FROM incident.incidents ORDER BY "
                   "tenant_id,grouping_key,group_until_ns DESC,incident_id DESC)")),
                 ("CREATE UNIQUE INDEX incident_current_group ON incident.incidents(tenant_id,grouping_key) WHERE "
                  "grouping_active"),
                 ("CREATE TABLE incident.types(tenant uuid NOT NULL,id uuid NOT NULL,version bigint NOT NULL,body "
                  "jsonb NOT NULL,PRIMARY KEY(tenant,id))"),
                 ("CREATE TABLE incident.signals(tenant uuid NOT NULL,id uuid NOT NULL,incident uuid NOT "
                  "NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,id))"),
                 "CREATE INDEX incident_signal_parent ON incident.signals(tenant,incident,id)",
                 ("CREATE TABLE incident.timeline(tenant uuid NOT NULL,incident uuid NOT NULL,id uuid NOT "
                  "NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,incident,id))"),
                 ("CREATE TABLE incident.requests(tenant uuid NOT NULL,actor text NOT NULL,key uuid NOT "
                  "NULL,fingerprint text NOT NULL,response jsonb NOT NULL,PRIMARY KEY(tenant,actor,key))"),
                 ("CREATE INDEX incident_auto_close ON incident.incidents((body->>'autoCloseAt')) WHERE "
                  "body->>'state'='INCIDENT_STATE_RESOLVED'")}},
           {.version = 3,
            .name = "immutable incident type versions",
            .expand = {("CREATE TABLE incident.type_versions(tenant uuid NOT NULL,id uuid NOT NULL,version bigint NOT "
                        "NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,id,version))"),
                       "INSERT INTO incident.type_versions SELECT tenant,id,version,body FROM incident.types"}},
           {.version = 4,
            .name = "signal preview counters",
            .expand = {(
                "UPDATE incident.incidents SET body=body || "
                "jsonb_build_object('signalCount',jsonb_array_length(COALESCE(body->'signalIds','[]'::jsonb)))")}},
           {.version = 5,
            .name = "response run versions",
            .expand = {("CREATE TABLE incident.responses(tenant uuid NOT NULL,incident uuid NOT NULL,body jsonb NOT "
                        "NULL,PRIMARY KEY(tenant,incident))")}},
           {.version = 6,
            .name = "command timeline versions",
            .expand = {"CREATE TABLE incident.command_versions(tenant uuid NOT NULL,id uuid NOT NULL,version bigint "
                       "NOT NULL,PRIMARY KEY(tenant,id,version))"}},
           {.version = 7,
            .name = "notification timeline versions",
            .expand = {"CREATE TABLE incident.notification_versions(tenant uuid NOT NULL,id uuid NOT NULL,version "
                       "bigint NOT NULL,PRIMARY KEY(tenant,id,version))"}},
           {.version = 8,
            .name = "operational state and immutable snapshots",
            .expand = {
                "ALTER TABLE incident.incidents ADD COLUMN operational_until timestamptz",
                ("CREATE INDEX incident_operational_expiry ON incident.incidents(operational_until) WHERE "
                 "operational_until IS NOT NULL"),
                ("CREATE TABLE incident.snapshots(tenant uuid NOT NULL,incident uuid NOT NULL,version bigint NOT "
                 "NULL,captured timestamptz NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,incident,version))"),
                "CREATE INDEX incident_snapshot_time ON incident.snapshots(tenant,incident,captured)",
                ("INSERT INTO incident.snapshots SELECT "
                 "tenant_id,incident_id,(body->>'version')::bigint,clock_timestamp(),body FROM incident.incidents"),
                ("CREATE FUNCTION incident.capture_snapshot() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
                 "INSERT INTO incident.snapshots "
                 "VALUES(NEW.tenant_id,NEW.incident_id,(NEW.body->>'version')::bigint,clock_timestamp(),NEW.body) ON "
                 "CONFLICT DO NOTHING; "
                 "RETURN NEW; END $$"),
                ("CREATE TRIGGER incident_snapshot AFTER INSERT OR UPDATE OF body ON incident.incidents FOR EACH ROW "
                 "WHEN ((NEW.body->>'version')::bigint > 0) EXECUTE FUNCTION incident.capture_snapshot()"),
                ("CREATE FUNCTION incident.next_operational_expiry() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
                 "NEW.operational_until=LEAST((NEW.body#>>'{operational,shelved,until}')::timestamptz,(NEW.body#>>'{"
                 "operational,suppressed,until}')::timestamptz,"
                 "(SELECT MIN((entry->>'until')::timestamptz) FROM "
                 "jsonb_array_elements(COALESCE(NEW.body#>'{operational,presence}','[]'::jsonb)) entry)); RETURN NEW; "
                 "END $$"),
                ("CREATE TRIGGER incident_operational_deadline BEFORE INSERT OR UPDATE OF body ON incident.incidents "
                 "FOR EACH ROW EXECUTE FUNCTION incident.next_operational_expiry()"),
                ("CREATE TABLE incident.exercises(tenant uuid NOT NULL,actor text NOT NULL,id uuid NOT NULL,incident "
                 "uuid NOT NULL,expires timestamptz NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,actor,id))"),
                "CREATE INDEX incident_exercise_expiry ON incident.exercises(expires)"}}});
}

asio::awaitable<p::Result<void>> Repository::audit(pg::Transaction* tx, p::security::Principal principal,
                                                   std::string action, std::string id, std::string site, bool refused,
                                                   std::string attempted, Json details) {
  audit::v1::AuditEventRecord record;
  envelope(record.mutable_envelope(), principal);
  auto* entry = record.mutable_audit_entry();
  *entry->mutable_actor() = record.envelope().actor();
  entry->set_action(std::move(action));
  entry->set_object_type("incident");
  entry->set_object_id(std::move(id));
  entry->set_site_id(std::move(site));
  if (refused) {
    (*entry->mutable_details())["attempted_action"] = std::move(attempted);
  }
  for (const auto& [name, value] : details.items()) {
    (*entry->mutable_details())[name] = value.is_string() ? value.get<std::string>() : value.dump();
  }
  entry->set_outcome(refused ? audit::v1::AUDIT_OUTCOME_DENIED : audit::v1::AUDIT_OUTCOME_SUCCESS);
  co_return co_await messages_.append(
      tx, {record.envelope().message_id(), encode_("psim.audit.v1", principal.tenant, record)});
}

asio::awaitable<p::Result<void>> Repository::denied(p::security::Principal principal, std::string action,
                                                    std::string id) {
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
      [this, &principal, &action, &id](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        co_return co_await audit(tx, principal, "access.denied", id, {}, true, action);
      });
}

asio::awaitable<p::Result<void>> Repository::save(pg::Transaction* tx, p::security::Principal principal,
                                                  incident::v1::Incident value, std::string action, std::string ref,
                                                  std::string text, std::string previous, const Encode* outgoing,
                                                  const common::v1::Envelope* cause) {
  if (!value.operational().has_condition_at()) {
    auto* state = value.mutable_operational();
    state->set_active(value.state() < incident::v1::INCIDENT_STATE_RESOLVED);
    state->set_acknowledged(value.has_acknowledged_at());
    *state->mutable_condition_at() = value.created_at();
  }
  if (action == "signal" && !value.operational().active()) {
    value.mutable_operational()->set_active(true);
    value.mutable_operational()->set_acknowledged(false);
    value.mutable_operational()->clear_acknowledged_by();
    *value.mutable_operational()->mutable_condition_at() = Time::GetCurrentTime();
  }
  if (action == "signal" || action == "created") {
    value.mutable_operational()->set_condition_known(true);
    *value.mutable_operational()->mutable_condition_at() = cause ? cause->occurred_at() : Time::GetCurrentTime();
  }
  auto updated = co_await tx->connection().query(
      {.sql = "UPDATE incident.incidents SET body=$3::jsonb,grouping_active=grouping_active AND $4::boolean WHERE "
              "tenant_id=$1::uuid AND incident_id=$2::uuid",
       .parameters = {Param::text(principal.tenant), Param::text(value.incident_id()), Param::text(json(value).dump()),
                      Param::boolean(value.state() < incident::v1::INCIDENT_STATE_RESOLVED)}});
  if (!updated) {
    co_return std::unexpected(updated.error());
  }
  incident::v1::IncidentEventRecord record;
  envelope(record.mutable_envelope(), principal, value.version());
  if (cause) {
    record.mutable_envelope()->set_causation_id(cause->message_id());
    record.mutable_envelope()->set_correlation_id(cause->correlation_id().empty() ? cause->message_id()
                                                                                  : cause->correlation_id());
  }
  google::protobuf::Message* change = nullptr;
  if (action == "created") {
    auto* change_value = record.mutable_incident_created();
    change_value->set_signal_id(ref);
    change = change_value;
  } else if (action == "signal") {
    auto* change_value = record.mutable_signal_attached();
    change_value->set_signal_id(ref);
    change = change_value;
  } else if (action == "acknowledge") {
    change = record.mutable_incident_acknowledged();
  } else if (action == "start-work") {
    change = record.mutable_incident_work_started();
  } else if (action == "assign") {
    auto* change_value = record.mutable_incident_assigned();
    change_value->set_previous_assignee_id(previous);
    change = change_value;
  } else if (action == "set-priority") {
    auto* change_value = record.mutable_incident_priority_changed();
    change_value->set_previous_priority(static_cast<common::v1::Priority>(std::stoi(previous)));
    change = change_value;
  } else if (action == "resolve") {
    change = record.mutable_incident_resolved();
  } else if (action == "reopen") {
    auto* change_value = record.mutable_incident_reopened();
    change_value->set_reason(text);
    change = change_value;
  } else if (action == "close") {
    change = record.mutable_incident_closed();
  } else if (action == "request-transfer") {
    change = record.mutable_incident_transfer_requested();
  } else if (action == "confirm-transfer") {
    auto* change_value = record.mutable_incident_transfer_confirmed();
    change_value->set_previous_assignee_id(previous);
    change_value->set_transfer_id(ref);
    change = change_value;
  } else if (action.starts_with("operational.")) {
    auto* operational_change = record.mutable_incident_operational_updated();
    operational_change->set_action(action);
    change = operational_change;
  } else if (action == "blocking") {
    change = record.mutable_incident_blocking_steps_changed();
  } else if (action == "escalate") {
    auto* escalation = record.mutable_incident_escalated();
    escalation->set_sla_kind(value.state() == incident::v1::INCIDENT_STATE_NEW ? "ack" : "resolve");
    change = escalation;
  } else if (action == "comment") {
    change = record.mutable_incident_comment_added();
  } else if (action == "link") {
    auto* change_value = record.mutable_incident_link_added();
    change_value->mutable_link()->set_link_id(ref);
    change_value->mutable_link()->set_url(text);
    change_value->mutable_link()->set_title(previous);
    change = change_value;
  } else {
    co_return p::fail(Code::kCommonInternal, "unknown incident event");
  }
  change->GetReflection()
      ->MutableMessage(change, change->GetDescriptor()->FindFieldByName("incident"))
      ->CopyFrom(value);
  incident::v1::TimelineEntry entry;
  entry.set_entry_id(record.envelope().message_id());
  entry.set_incident_id(value.incident_id());
  *entry.mutable_at() = record.envelope().occurred_at();
  *entry.mutable_actor() = record.envelope().actor();
  entry.set_text(text);
  entry.set_ref_id(ref);
  auto kind = incident::v1::TIMELINE_ENTRY_KIND_STATE_CHANGED;
  if (action == "comment") {
    kind = incident::v1::TIMELINE_ENTRY_KIND_COMMENT;
  } else if (action == "link") {
    kind = incident::v1::TIMELINE_ENTRY_KIND_LINK;
  } else if (action == "signal" || action == "created") {
    kind = incident::v1::TIMELINE_ENTRY_KIND_SIGNAL_ATTACHED;
  } else if (action == "set-priority") {
    kind = incident::v1::TIMELINE_ENTRY_KIND_PRIORITY_CHANGED;
  } else if (action == "assign" || action == "confirm-transfer" || action == "request-transfer") {
    kind = incident::v1::TIMELINE_ENTRY_KIND_ASSIGNED;
  }
  if (action == "escalate") {
    kind = incident::v1::TIMELINE_ENTRY_KIND_ESCALATED;
  }
  if (action == "blocking") {
    kind = incident::v1::TIMELINE_ENTRY_KIND_STEP;
  }
  entry.set_kind(kind);
  (*entry.mutable_details())["action"] = action;
  (*entry.mutable_details())["version"] = std::to_string(value.version());
  if (action == "comment") {
    *record.mutable_incident_comment_added()->mutable_entry() = entry;
  }
  auto timeline = co_await tx->connection().query(
      {.sql = "INSERT INTO incident.timeline VALUES($1::uuid,$2::uuid,$3::uuid,$4::jsonb)",
       .parameters = {Param::text(principal.tenant), Param::text(value.incident_id()), Param::text(entry.entry_id()),
                      Param::text(json(entry).dump())}});
  if (!timeline) {
    co_return std::unexpected(timeline.error());
  }
  auto emitted = co_await messages_.append(
      tx, {record.envelope().message_id(),
           (outgoing ? *outgoing : encode_)("psim.incidents.events.v1", value.incident_id(), record)});
  if (!emitted) {
    co_return emitted;
  }
  Json details = Json::object();
  if (action.starts_with("operational.")) {
    const auto& state = value.operational();
    details = {{"version", std::to_string(value.version())},
               {"reason", text},
               {"reference", ref},
               {"active", state.active()},
               {"acknowledged", state.acknowledged()},
               {"presence_count", state.presence_size()}};
    if (state.has_shelved()) {
      details["shelved_until"] = Time::ToString(state.shelved().until());
    }
    if (state.has_suppressed()) {
      details["suppressed_until"] = Time::ToString(state.suppressed().until());
    }
    if (state.has_acknowledge_due()) {
      details["acknowledge_due"] = Time::ToString(state.acknowledge_due());
    }
    if (state.has_resolve_due()) {
      details["resolve_due"] = Time::ToString(state.resolve_due());
    }
  }
  co_return co_await audit(tx, principal, "incident." + action, value.incident_id(), value.site_id(), false, {},
                           details);
}

asio::awaitable<p::Result<bool>> Repository::apply(processing::v1::SignalRecord signal, Encode outgoing) {
  const auto& input = signal.envelope();
  const auto& incoming = signal.signal();
  if (!p::stream::uuid(input.tenant_id()) || !p::stream::uuid(input.message_id())
      || !p::stream::uuid(incoming.site_id()) || !p::stream::uuid(incoming.incident_type_id())
      || (!incoming.zone_id().empty() && !p::stream::uuid(incoming.zone_id())) || incoming.grouping_key().empty()
      || incoming.grouping_key().size() > 1024 || incoming.priority() < 1 || incoming.priority() > 4) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid signal identity or priority");
  }
  co_return co_await pg::in_transaction<bool>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
      [this, &signal, &outgoing](pg::Transaction* tx) -> asio::awaitable<p::Result<bool>> {
        const auto& origin = signal.envelope();
        const auto& source = signal.signal();
        auto accepted = co_await messages_.accept(tx, "signals", origin.tenant_id() + ":" + origin.message_id(),
                                                  p::stream::sha256(origin.tenant_id() + source.SerializeAsString()));
        if (!accepted) {
          co_return std::unexpected(accepted.error());
        }
        if (!*accepted) {
          // Reconstruct available signal bodies for incidents created by the earlier implementation.
          auto restored = co_await tx->connection().query(
              {.sql = "INSERT INTO incident.signals(tenant,id,incident,body) SELECT "
                      "$1::uuid,$2::uuid,incident_id,$3::jsonb FROM incident.incidents WHERE tenant_id=$1::uuid AND "
                      "body->'signalIds' ? $2::text ON CONFLICT(tenant,id) DO NOTHING",
               .parameters = {Param::text(origin.tenant_id()), Param::text(origin.message_id()),
                              Param::text(json(signal).dump())}});
          if (!restored) {
            co_return std::unexpected(restored.error());
          }
          co_return false;
        }
        auto locked = co_await tx->connection().query(
            {.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
             .parameters = {Param::text(origin.tenant_id() + ":" + source.grouping_key())}});
        if (!locked) {
          co_return std::unexpected(locked.error());
        }
        incident::v1::IncidentType type;
        auto types = co_await tx->connection().query(
            {.sql = "SELECT body::text FROM incident.types WHERE tenant=$1::uuid AND id=$2::uuid",
             .parameters = {Param::text(origin.tenant_id()), Param::text(source.incident_type_id())}});
        if (!types) {
          co_return std::unexpected(types.error());
        }
        if (types->size() > 0) {
          auto parsed = parse<incident::v1::IncidentType>(Json::parse(types->value(0, 0).value_or("")));
          if (!parsed) {
            co_return std::unexpected(parsed.error());
          }
          type = std::move(*parsed);
        } else if (legacy_types_) {
          type.set_incident_type_id(source.incident_type_id());
          type.mutable_grouping_window()->set_seconds(60);
          type.mutable_auto_close_after()->set_seconds(900);
        } else {
          co_return p::fail(Code::kProcessingPayloadInvalid, "incident type not configured");
        }
        const auto first = source.has_first_event_at() ? source.first_event_at() : origin.occurred_at();
        if (first.seconds() < 0 || first.seconds() > 9000000000LL || first.nanos() < 0 || first.nanos() >= 1000000000) {
          co_return p::fail(Code::kProcessingPayloadInvalid, "invalid grouping timestamp");
        }
        const auto when = Time::TimestampToNanoseconds(first);
        const auto window = Time::DurationToNanoseconds(type.grouping_window());
        if (window <= 0 || when > std::numeric_limits<std::int64_t>::max() - window || when < 0) {
          co_return p::fail(Code::kProcessingPayloadInvalid, "invalid grouping time");
        }
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT body::text FROM incident.incidents WHERE tenant_id=$1::uuid AND grouping_key=$2 AND "
                    "grouping_active FOR UPDATE",
             .parameters = {Param::text(origin.tenant_id()), Param::text(source.grouping_key())}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        incident::v1::Incident value;
        bool created = rows->size() == 0;
        if (!created) {
          auto parsed = parse<incident::v1::Incident>(Json::parse(rows->value(0, 0).value_or("")));
          if (!parsed) {
            co_return std::unexpected(parsed.error());
          }
          value = std::move(*parsed);
          if (value.site_id() != source.site_id() || value.zone_id() != source.zone_id()
              || value.incident_type_id() != source.incident_type_id()) {
            co_return p::fail(Code::kProcessingPayloadInvalid, "grouping key resource mismatch");
          }
          created =
              !d::accepts_signal({.active = true, .until = Time::TimestampToNanoseconds(value.group_until())}, when);
        }
        std::string related;
        if (created) {
          if (!value.incident_id().empty()) {
            related = value.incident_id();
          } else {
            auto prior = co_await tx->connection().query(
                {.sql = "SELECT incident_id::text FROM incident.incidents WHERE tenant_id=$1::uuid AND grouping_key=$2 "
                        "ORDER BY group_until_ns DESC,incident_id DESC LIMIT 1",
                 .parameters = {Param::text(origin.tenant_id()), Param::text(source.grouping_key())}});
            if (!prior) {
              co_return std::unexpected(prior.error());
            }
            if (prior->size() > 0) {
              related = std::string(prior->value(0, 0).value_or(""));
            }
          }
          auto expired = co_await tx->connection().query(
              {.sql = "UPDATE incident.incidents SET grouping_active=false WHERE tenant_id=$1::uuid AND "
                      "grouping_key=$2 AND grouping_active",
               .parameters = {Param::text(origin.tenant_id()), Param::text(source.grouping_key())}});
          if (!expired) {
            co_return std::unexpected(expired.error());
          }
          value.Clear();
          value.set_incident_id(p::stream::uuid7());
          value.set_incident_type_id(source.incident_type_id());
          value.set_incident_type_version(type.version());
          value.set_escalation_policy_id(type.escalation_policy_id());
          value.set_site_id(source.site_id());
          value.set_zone_id(source.zone_id());
          value.set_state(incident::v1::INCIDENT_STATE_NEW);
          value.set_priority(source.priority());
          value.set_priority_source(incident::v1::PRIORITY_SOURCE_COMPUTED);
          value.set_grouping_key(source.grouping_key());
          *value.mutable_group_until() = Time::NanosecondsToTimestamp(when + window);
          *value.mutable_created_at() = Time::GetCurrentTime();
          value.set_title(source.summary());
          value.set_related_incident_id(related);
          value.mutable_operational()->set_active(true);
          value.mutable_operational()->set_condition_known(true);
          *value.mutable_operational()->mutable_condition_at() = value.created_at();
          auto inserted = co_await tx->connection().query(
              {.sql = "INSERT INTO "
                      "incident.incidents(tenant_id,incident_id,grouping_key,group_until_ns,body,grouping_active) "
                      "VALUES($1::uuid,$2::uuid,$3,$4,$5::jsonb,true)",
               .parameters = {Param::text(origin.tenant_id()), Param::text(value.incident_id()),
                              Param::text(value.grouping_key()), Param::integer(when + window),
                              Param::text(json(value).dump())}});
          if (!inserted) {
            co_return std::unexpected(inserted.error());
          }
        }
        if (!created && value.incident_type_version() != 0) {
          auto history = co_await tx->connection().query(
              {.sql =
                   "SELECT body::text FROM incident.type_versions WHERE tenant=$1::uuid AND id=$2::uuid AND version=$3",
               .parameters = {Param::text(origin.tenant_id()), Param::text(value.incident_type_id()),
                              Param::integer(static_cast<std::int64_t>(value.incident_type_version()))}});
          if (!history) {
            co_return std::unexpected(history.error());
          }
          if (history->size() != 1) {
            co_return p::fail(Code::kCommonInternal, "incident type history missing");
          }
          auto pinned = parse<incident::v1::IncidentType>(Json::parse(history->value(0, 0).value_or("")));
          if (!pinned) {
            co_return std::unexpected(pinned.error());
          }
          type = std::move(*pinned);
        }
        auto priority = source.priority();
        if (type.priority_matrix().contains(static_cast<int>(priority))) {
          priority = type.priority_matrix().at(static_cast<int>(priority));
        }
        value.set_priority(static_cast<common::v1::Priority>(d::raised_priority(
            created ? static_cast<int>(priority) : static_cast<int>(value.priority()), static_cast<int>(priority))));
        // Signal bodies and identities are authoritative in the separate paginated table.
        if (value.signal_ids_size() < 1000) {
          value.add_signal_ids(origin.message_id());
        }
        value.set_signal_count(value.signal_count() + 1);
        value.set_signal_ids_truncated(value.signal_count() > 1000);
        value.set_version(value.version() + 1);
        auto stored = co_await tx->connection().query(
            {.sql = "INSERT INTO incident.signals VALUES($1::uuid,$2::uuid,$3::uuid,$4::jsonb)",
             .parameters = {Param::text(origin.tenant_id()), Param::text(origin.message_id()),
                            Param::text(value.incident_id()), Param::text(json(signal).dump())}});
        if (!stored) {
          co_return std::unexpected(stored.error());
        }
        auto saved = co_await save(tx, system(origin.tenant_id()), value, created ? "created" : "signal",
                                   origin.message_id(), {}, {}, outgoing ? &outgoing : nullptr, &origin);
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        co_return true;
      });
}

asio::awaitable<p::Result<Json>> Repository::request(p::security::Principal principal, std::string operation,
                                                     std::string id, Json body, std::string key,
                                                     std::optional<std::uint64_t> expected) {
  if (!p::stream::uuid(principal.tenant) || (!id.empty() && !p::stream::uuid(id))
      || (!key.empty() && !p::stream::uuid(key))) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid identity");
  }
  const bool writing = operation != "get" && operation != "list" && operation != "timeline" && operation != "signals"
                       && operation != "types.list" && operation != "operations" && operation != "snapshots"
                       && operation != "recovery" && operation != "get-exercise";
  const bool mutation = writing && !key.empty();
  if (writing && key.empty() && operation != "types.update") {
    co_return p::fail(Code::kCommonInvalidArgument, "UUID Idempotency-Key required");
  }
  Json prepared;
  if (operation == "ask-assistant") {
    auto snapshot = co_await request(principal, "get", id, Json::object());
    if (!snapshot) {
      co_return std::unexpected(snapshot.error());
    }
    auto value = parse<incident::v1::Incident>(snapshot->at("incident"));
    if (!value) {
      co_return std::unexpected(value.error());
    }
    auto allowed =
        p::authz::decide(principal, "incident.ask-assistant", {principal.tenant, value->site_id(), value->zone_id()},
                         std::chrono::system_clock::now());
    if (!allowed) {
      co_return std::unexpected(allowed.error());
    }
    if (!expected || *expected != value->version()) {
      auto logged = co_await denied(principal, "assistant.source-version", id);
      if (!logged) {
        co_return std::unexpected(logged.error());
      }
      co_return p::fail(expected ? Code::kCommonConcurrentModification : Code::kCommonPreconditionRequired,
                        "assistant source version required");
    }
    auto answer = co_await prepare_assistant(principal, *value, body);
    if (!answer) {
      auto logged = co_await denied(principal, "ask-assistant", id);
      if (!logged) {
        co_return std::unexpected(logged.error());
      }
      co_return std::unexpected(answer.error());
    }
    prepared = std::move(*answer);
  }
  auto result = co_await pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
      [this, &principal, &operation, &id, &body, &key, &expected, mutation,
       &prepared](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        // The request lock serializes repeats across instances without serializing all tenant writes.
        if (mutation) {
          auto locked =
              co_await tx->connection().query({.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                               .parameters = {Param::text("incident-request:" + principal.tenant + ":"
                                                                          + principal.subject + ":" + key)}});
          if (!locked) {
            co_return std::unexpected(locked.error());
          }
        }
        const auto fingerprint = p::stream::sha256(operation + ":" + id + ":" + body.dump() + ":"
                                                   + (expected ? std::to_string(*expected) : ""));
        if (mutation) {
          auto cached = co_await tx->connection().query(
              {.sql = "SELECT fingerprint,response::text FROM incident.requests WHERE tenant=$1::uuid AND actor=$2 AND "
                      "key=$3::uuid",
               .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key)}});
          if (!cached) {
            co_return std::unexpected(cached.error());
          }
          if (cached->size() > 0) {
            if (cached->value(0, 0).value_or("") != fingerprint) {
              co_return p::fail(Code::kCommonIdempotencyKeyReused, "idempotency key payload mismatch");
            }
            auto resource = p::authz::Resource{principal.tenant};
            std::optional<incident::v1::Incident> current;
            if (!id.empty() && !operation.starts_with("types.")) {
              auto loaded = co_await load(tx, principal.tenant, id);
              if (!loaded) {
                co_return std::unexpected(loaded.error());
              }
              current = std::move(*loaded);
              resource.site = current->site_id();
              resource.zone = current->zone_id();
            }
            auto allowed = p::authz::decide(
                principal, operation.starts_with("types.") ? "incident.types.write" : "incident." + operation, resource,
                std::chrono::system_clock::now());
            if (!allowed) {
              co_return std::unexpected(allowed.error());
            }
            auto response = Json::parse(cached->value(0, 1).value_or(""));
            if (operation == "update-alarm") {
              const auto action = body.value("action", std::string{});
              if (action == "activate" || action == "return" || action == "suppress" || action == "unsuppress") {
                auto permitted =
                    p::authz::decide(principal, "incident.alarm-control", resource, std::chrono::system_clock::now());
                if (!permitted) {
                  co_return std::unexpected(permitted.error());
                }
              }
            }
            if (operation == "set-presence" && !body.value("release", false) && current) {
              bool live = false;
              for (const auto& lease : current->operational().presence()) {
                if (lease.actor_id() == principal.subject && lease.session_id() == body.value("sessionId", "")
                    && Time::TimestampToMilliseconds(lease.until())
                           > Time::TimestampToMilliseconds(Time::GetCurrentTime())) {
                  const auto& returned = response.at("incident").at("operational").at("presence");
                  live = std::ranges::any_of(returned, [&lease](const Json& item) {
                    return item.value("leaseId", std::string{}) == lease.lease_id();
                  });
                }
              }
              if (!live) {
                co_return p::fail(Code::kCommonConcurrentModification, "presence lease expired");
              }
            }
            if (operation == "ask-assistant" && current && expected != current->version()) {
              co_return p::fail(Code::kCommonConcurrentModification, "assistant source changed");
            }
            if (response.contains("exercise")) {
              google::protobuf::Timestamp until;
              if (!Time::FromString(response["exercise"].value("expiresAt", ""), &until)
                  || Time::TimestampToMilliseconds(until) <= Time::TimestampToMilliseconds(Time::GetCurrentTime())) {
                co_return p::fail(Code::kCommonNotFound, "exercise expired");
              }
            }
            co_return response;
          }
        }
        auto response = co_await execute(tx, principal, operation, id, body, expected, prepared);
        if (!response) {
          co_return response;
        }
        if (mutation) {
          auto saved = co_await tx->connection().query(
              {.sql = "INSERT INTO incident.requests VALUES($1::uuid,$2,$3::uuid,$4,$5::jsonb)",
               .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key),
                              Param::text(fingerprint), Param::text(response->dump())}});
          if (!saved) {
            co_return std::unexpected(saved.error());
          }
        }
        co_return response;
      });
  if (!result
      && (result.error().code() == Code::kAuthForbidden || result.error().code() == Code::kAuthScopeDenied
          || result.error().code() == Code::kIncidentResolutionForbidden
          || (operation == "ask-assistant" && result.error().code() == Code::kCommonConcurrentModification))) {
    auto recorded = co_await denied(principal, operation, id);
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
  }
  co_return result;
}

asio::awaitable<p::Result<Json>> Repository::execute(pg::Transaction* tx, p::security::Principal principal,
                                                     std::string operation, std::string id, Json body,
                                                     std::optional<std::uint64_t> expected, Json prepared) {
  const auto now = std::chrono::system_clock::now();
  if (operation.starts_with("types.")) {
    auto allowed = p::authz::decide(
        principal, operation == "types.list" ? "incident.types.read" : "incident.types.write", {principal.tenant}, now);
    if (!allowed) {
      co_return std::unexpected(allowed.error());
    }
    if (operation == "types.list") {
      const auto size = body.value("pageSize", 100);
      const auto after = body.value("pageToken", "");
      if (size < 1 || size > 1000 || (!after.empty() && !p::stream::uuid(after))) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid page");
      }
      auto rows = co_await tx->connection().query(
          {.sql = "SELECT body::text FROM incident.types WHERE tenant=$1::uuid AND ($2='' OR id::text>$2) ORDER BY id "
                  "LIMIT $3",
           .parameters = {Param::text(principal.tenant), Param::text(after), Param::integer(size + 1)}});
      if (!rows) {
        co_return std::unexpected(rows.error());
      }
      Json output = {{"incidentTypes", Json::array()}};
      for (std::size_t row = 0; row < std::min(rows->size(), static_cast<std::size_t>(size)); ++row) {
        output["incidentTypes"].push_back(Json::parse(rows->value(row, 0).value_or("")));
      }
      if (rows->size() > static_cast<std::size_t>(size)) {
        output["nextPageToken"] = output["incidentTypes"].back()["incidentTypeId"];
      }
      co_return output;
    }
    auto type = parse<incident::v1::IncidentType>(body);
    if (!type || (!type->escalation_policy_id().empty() && !p::stream::uuid(type->escalation_policy_id()))
        || !p::stream::uuid(type->incident_type_id()) || type->code().empty() || type->code().size() > 128
        || type->name().empty() || type->name().size() > 1024 || type->default_priority() < 1
        || type->default_priority() > 4 || Time::DurationToNanoseconds(type->grouping_window()) <= 0
        || Time::DurationToNanoseconds(type->grouping_window()) > 86400000000000LL
        || Time::DurationToNanoseconds(type->auto_close_after()) < 0
        || Time::DurationToNanoseconds(type->auto_close_after()) > 604800000000000LL) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid incident type");
    }
    for (const auto& [from, to] : type->priority_matrix()) {
      if (from < 1 || from > 4 || to < 1 || to > 4) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid priority mapping");
      }
    }
    if (!id.empty() && id != type->incident_type_id()) {
      co_return p::fail(Code::kCommonInvalidArgument, "incident type identity mismatch");
    }
    auto locked = co_await tx->connection().query(
        {.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
         .parameters = {Param::text("incident-type:" + principal.tenant + ":" + type->incident_type_id())}});
    if (!locked) {
      co_return std::unexpected(locked.error());
    }
    auto rows = co_await tx->connection().query(
        {.sql = "SELECT version FROM incident.types WHERE tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
         .parameters = {Param::text(principal.tenant), Param::text(type->incident_type_id())}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    std::uint64_t version = 0;
    if (operation == "types.create") {
      if (rows->size() != 0) {
        co_return p::fail(Code::kIncidentTypeExists, "incident type exists");
      }
    } else {
      if (!expected) {
        co_return p::fail(Code::kCommonPreconditionRequired, "If-Match required");
      }
      if (rows->size() == 0) {
        co_return p::fail(Code::kCommonNotFound, "incident type not found");
      }
      version = static_cast<std::uint64_t>(*rows->integer(0, 0));
      if (version != *expected) {
        co_return p::fail(Code::kCommonConcurrentModification, "incident type version changed");
      }
    }
    type->set_version(version + 1);
    if (!type->has_auto_close_after()) {
      type->mutable_auto_close_after()->set_seconds(900);
    }
    auto stored = co_await tx->connection().query(
        {.sql = "INSERT INTO incident.types VALUES($1::uuid,$2::uuid,$3,$4::jsonb) ON CONFLICT(tenant,id) DO UPDATE "
                "SET version=EXCLUDED.version,body=EXCLUDED.body",
         .parameters = {Param::text(principal.tenant), Param::text(type->incident_type_id()),
                        Param::integer(static_cast<std::int64_t>(type->version())), Param::text(json(*type).dump())}});
    if (!stored) {
      co_return std::unexpected(stored.error());
    }
    auto history = co_await tx->connection().query(
        {.sql = "INSERT INTO incident.type_versions VALUES($1::uuid,$2::uuid,$3,$4::jsonb)",
         .parameters = {Param::text(principal.tenant), Param::text(type->incident_type_id()),
                        Param::integer(static_cast<std::int64_t>(type->version())), Param::text(json(*type).dump())}});
    if (!history) {
      co_return std::unexpected(history.error());
    }
    config::v1::ConfigRecord record;
    envelope(record.mutable_envelope(), principal, type->version());
    *record.mutable_incident_type() = *type;
    auto emitted = co_await messages_.append(
        tx, {record.envelope().message_id(),
             encode_("psim.config.v1", "incident-type/" + principal.tenant + "/" + type->incident_type_id(), record)});
    if (!emitted) {
      co_return std::unexpected(emitted.error());
    }
    auto audited = co_await audit(tx, principal, "incident." + operation, type->incident_type_id());
    if (!audited) {
      co_return std::unexpected(audited.error());
    }
    co_return Json{{"incidentType", json(*type)}};
  }
  if (operation == "list") {
    // Role and tenant are checked independently of per-row scope filtering.
    auto role = p::authz::decide(principal, "incident.read", {principal.tenant}, now);
    if (!role && role.error().code() != Code::kAuthScopeDenied) {
      co_return std::unexpected(role.error());
    }
    const auto size = body.value("pageSize", 100);
    const auto after = body.value("pageToken", "");
    if (size < 1 || size > 1000 || (!after.empty() && !p::stream::uuid(after))) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid page");
    }
    const auto scope = p::authz::scope_filter(principal);
    auto rows = co_await tx->connection().query(
        {.sql =
             R"sql(SELECT body::text FROM incident.incidents WHERE tenant_id=$1::uuid AND ($2='' OR incident_id::text>$2)
      AND ($3::boolean OR body->>'siteId' IN(SELECT jsonb_array_elements_text($4::jsonb)) OR body->>'zoneId' IN(SELECT jsonb_array_elements_text($7::jsonb)))
      AND (NOT ($5::jsonb ? 'states') OR body->>'state' IN(SELECT jsonb_array_elements_text($5::jsonb->'states')))
      AND (NOT ($5::jsonb ? 'priorities') OR body->>'priority' IN(SELECT jsonb_array_elements_text($5::jsonb->'priorities')))
      AND (NOT ($5::jsonb ? 'siteIds') OR body->>'siteId' IN(SELECT jsonb_array_elements_text($5::jsonb->'siteIds')))
      AND (NOT ($5::jsonb ? 'zoneIds') OR body->>'zoneId' IN(SELECT jsonb_array_elements_text($5::jsonb->'zoneIds')))
      AND (NOT ($5::jsonb ? 'incidentTypeIds') OR body->>'incidentTypeId' IN(SELECT jsonb_array_elements_text($5::jsonb->'incidentTypeIds')))
      AND (NOT ($5::jsonb ? 'assigneeId') OR body->>'assigneeId'=$5::jsonb->>'assigneeId')
      AND (NOT COALESCE(($5::jsonb->>'escalatedOnly')::boolean,false) OR (body->>'escalationLevel')::integer>0)
      AND (NOT ($5::jsonb ? 'createdFrom') OR (body->>'createdAt')::timestamptz>=($5::jsonb->>'createdFrom')::timestamptz)
      AND (NOT ($5::jsonb ? 'createdTo') OR (body->>'createdAt')::timestamptz<=($5::jsonb->>'createdTo')::timestamptz)
      ORDER BY incident_id LIMIT $6)sql",
         .parameters = {Param::text(principal.tenant), Param::text(after), Param::boolean(scope.all),
                        Param::text(Json(scope.sites).dump()), Param::text(body.dump()), Param::integer(size + 1),
                        Param::text(Json(scope.zones).dump())}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    Json output = {{"incidents", Json::array()}};
    for (std::size_t row = 0; row < std::min(rows->size(), static_cast<std::size_t>(size)); ++row) {
      output["incidents"].push_back(Json::parse(rows->value(row, 0).value_or("")));
    }
    if (rows->size() > static_cast<std::size_t>(size)) {
      output["nextPageToken"] = output["incidents"].back()["incidentId"];
    }
    co_return output;
  }
  auto loaded = co_await load(tx, principal.tenant, id);
  if (!loaded) {
    co_return std::unexpected(loaded.error());
  }
  auto value = std::move(*loaded);
  auto allowed = p::authz::decide(principal,
                                  operation == "get" || operation == "timeline" || operation == "signals"
                                          || operation == "operations" || operation == "snapshots"
                                          || operation == "recovery" || operation == "get-exercise"
                                      ? "incident.read"
                                      : "incident." + operation,
                                  {principal.tenant, value.site_id(), value.zone_id()}, now);
  if (!allowed) {
    co_return std::unexpected(allowed.error());
  }
  if (operation == "operations" || operation == "snapshots" || operation == "recovery" || operation == "get-exercise"
      || operation == "update-alarm" || operation == "set-presence" || operation == "ask-assistant"
      || operation == "create-exercise" || operation == "act-exercise") {
    co_return co_await operational(tx, principal, operation, std::move(value), body, expected, std::move(prepared));
  }
  if (operation == "get") {
    co_return Json{{"incident", json(value)}};
  }
  if (operation == "timeline" || operation == "signals") {
    const auto size = body.value("pageSize", 100);
    const auto after = body.value("pageToken", "");
    if (size < 1 || size > 1000 || (!after.empty() && !p::stream::uuid(after))) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid page");
    }
    pg::Query query{
        .sql = operation == "timeline" ? "SELECT id::text,body::text FROM incident.timeline WHERE tenant=$1::uuid AND "
                                         "incident=$2::uuid AND ($3='' OR id::text>$3) ORDER BY id LIMIT $4"
                                       : "SELECT id::text,body::text FROM incident.signals WHERE tenant=$1::uuid AND "
                                         "incident=$2::uuid AND ($3='' OR id::text>$3) ORDER BY id LIMIT $4",
        .parameters = {Param::text(principal.tenant), Param::text(id), Param::text(after), Param::integer(size + 1)}};
    auto rows = co_await tx->connection().query(std::move(query));
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    const auto* field = operation == "timeline" ? "entries" : "signals";
    Json output = {{field, Json::array()}};
    for (std::size_t row = 0; row < std::min(rows->size(), static_cast<std::size_t>(size)); ++row) {
      auto entry = Json::parse(rows->value(row, 1).value_or(""));
      if (operation == "signals") {
        entry = {{"signalId", std::string(rows->value(row, 0).value_or(""))},
                 {"signal", entry["signal"]},
                 {"attachedAt", entry["envelope"]["producedAt"]}};
      }
      output[field].push_back(std::move(entry));
    }
    if (rows->size() > static_cast<std::size_t>(size)) {
      output["nextPageToken"] = std::string(rows->value(static_cast<std::size_t>(size) - 1, 0).value_or(""));
    }
    co_return output;
  }
  if (!expected) {
    co_return p::fail(Code::kCommonPreconditionRequired, "If-Match required");
  }
  if (value.version() != *expected) {
    co_return p::fail(Code::kCommonConcurrentModification, "incident version changed");
  }
  std::string recipient =
      operation == "acknowledge" ? principal.subject : body.value("assigneeId", body.value("recipientId", ""));
  if (operation == "confirm-transfer") {
    if (!value.has_pending_transfer() || value.pending_transfer().transfer_id() != body.value("transferId", "")
        || value.pending_transfer().recipient_id() != principal.subject
        || value.pending_transfer().requested_version() != value.version()) {
      co_return p::fail(Code::kCommonConcurrentModification, "transfer changed or caller is not recipient");
    }
    recipient = principal.subject;
  }
  if (operation == "reopen") {
    recipient = principal.roles.contains("supervisor") ? principal.subject : value.assignee_id();
  }
  bool valid = false;
  if (!recipient.empty()) {
    if (!lookup_) {
      co_return p::fail(Code::kCommonUnavailable, "identity directory not configured");
    }
    auto target = co_await lookup_(recipient);
    if (!target) {
      co_return std::unexpected(target.error());
    }
    valid = static_cast<bool>(
        p::authz::decide(*target, "incident.acknowledge", {principal.tenant, value.site_id(), value.zone_id()}, now));
  }
  int resolution = 0;
  if (body.contains("resolution")) {
    incident::v1::Resolution parsed{};
    if (!incident::v1::Resolution_Parse(body["resolution"].get<std::string>(), &parsed)) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid resolution");
    }
    resolution = static_cast<int>(parsed);
  }
  int priority = 0;
  if (body.contains("priority")) {
    common::v1::Priority parsed{};
    if (!common::v1::Priority_Parse(body["priority"].get<std::string>(), &parsed)) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid priority");
    }
    priority = static_cast<int>(parsed);
  }
  const d::Value state{.state = static_cast<d::State>(value.state()),
                       .resolution = static_cast<d::Resolution>(value.resolution()),
                       .assignee = value.assignee_id(),
                       .priority = static_cast<int>(value.priority()),
                       .manual = value.priority_source() == incident::v1::PRIORITY_SOURCE_MANUAL,
                       .blocking = value.blocking_steps_size() != 0,
                       .run_version = value.response_run_version()};
  const d::Command command{.action = operation,
                           .actor = principal.subject,
                           .supervisor = principal.roles.contains("supervisor"),
                           .assignee = recipient,
                           .recipient_valid = valid,
                           .resolution = static_cast<d::Resolution>(resolution),
                           .run_version = body.contains("knownResponseRunVersion")
                                              ? std::stoull(body["knownResponseRunVersion"].get<std::string>())
                                              : 0,
                           .priority = priority};
  auto changed = d::apply(state, command);
  if (!changed) {
    co_return p::fail(failure(changed.error()), "incident command rejected");
  }
  if (operation == "reopen" && !valid) {
    co_return p::fail(Code::kIncidentAssigneeInvalid, "assignee no longer eligible");
  }
  const auto timestamp = Time::GetCurrentTime();
  if (Time::TimestampToNanoseconds(timestamp) < Time::TimestampToNanoseconds(value.created_at())) {
    co_return p::fail(Code::kCommonUnavailable, "clock moved before incident creation");
  }
  const auto previous =
      operation == "set-priority" ? std::to_string(static_cast<int>(value.priority())) : value.assignee_id();
  std::string ref;
  std::string text = body.value("text", body.value("note", body.value("reason", body.value("comment", ""))));
  value.set_state(static_cast<incident::v1::IncidentState>(changed->state));
  value.set_assignee_id(changed->assignee);
  value.set_priority(static_cast<common::v1::Priority>(changed->priority));
  value.set_priority_source(changed->manual ? incident::v1::PRIORITY_SOURCE_MANUAL
                                            : incident::v1::PRIORITY_SOURCE_COMPUTED);
  value.set_resolution(static_cast<incident::v1::Resolution>(changed->resolution));
  if (operation == "acknowledge") {
    *value.mutable_acknowledged_at() = timestamp;
  }
  if (operation == "resolve") {
    if (value.has_acknowledged_at()
        && Time::TimestampToNanoseconds(timestamp) < Time::TimestampToNanoseconds(value.acknowledged_at())) {
      co_return p::fail(Code::kCommonUnavailable, "clock moved before acknowledgement");
    }
    *value.mutable_resolved_at() = timestamp;
    value.set_resolution_note(text);
    auto rows = co_await tx->connection().query(
        {.sql = "SELECT body::text FROM incident.type_versions WHERE tenant=$1::uuid AND id=$2::uuid AND version=$3",
         .parameters = {Param::text(principal.tenant), Param::text(value.incident_type_id()),
                        Param::integer(static_cast<std::int64_t>(value.incident_type_version()))}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    std::int64_t delay = 900000000000LL;
    if (rows->size() > 0) {
      auto type = parse<incident::v1::IncidentType>(Json::parse(rows->value(0, 0).value_or("")));
      if (!type) {
        co_return std::unexpected(type.error());
      }
      delay = Time::DurationToNanoseconds(type->auto_close_after());
    }
    *value.mutable_auto_close_at() = Time::NanosecondsToTimestamp(Time::TimestampToNanoseconds(timestamp) + delay);
  }
  if (operation == "reopen") {
    if (text.empty()) {
      co_return p::fail(Code::kCommonInvalidArgument, "reopen reason required");
    }
    if (!value.has_acknowledged_at()) {
      *value.mutable_acknowledged_at() = timestamp;
    }
    value.clear_resolved_at();
    value.clear_auto_close_at();
    value.clear_resolution_note();
  }
  if (operation == "close") {
    if (Time::TimestampToNanoseconds(timestamp) < Time::TimestampToNanoseconds(value.resolved_at())) {
      co_return p::fail(Code::kCommonUnavailable, "clock moved before resolution");
    }
    *value.mutable_closed_at() = timestamp;
    value.clear_auto_close_at();
  }
  if (operation == "comment" && (text.empty() || text.size() > 8000)) {
    co_return p::fail(Code::kCommonInvalidArgument, "comment size invalid");
  }
  if (operation == "link") {
    text = body.value("url", "");
    if (!text.starts_with("https://") || !p::security::validate_url(text) || text.size() > 2048
        || body.value("title", "").size() > 1024) {
      co_return p::fail(Code::kCommonInvalidArgument, "HTTPS link required");
    }
    ref = p::stream::uuid7();
  }
  const auto transfer_id = value.pending_transfer().transfer_id();
  value.clear_pending_transfer();
  value.set_version(value.version() + 1);
  if (operation == "request-transfer") {
    auto* transfer = value.mutable_pending_transfer();
    transfer->set_transfer_id(p::stream::uuid7());
    transfer->set_requested_by(principal.subject);
    transfer->set_recipient_id(recipient);
    transfer->set_comment(text);
    transfer->set_requested_version(value.version());
    ref = transfer->transfer_id();
  }
  if (operation == "confirm-transfer") {
    ref = transfer_id;
  }
  auto saved = co_await save(tx, principal, value, operation, ref, text,
                             operation == "link" ? body.value("title", "") : previous);
  if (!saved) {
    co_return std::unexpected(saved.error());
  }
  if (operation == "comment") {
    auto entry =
        co_await tx->connection().query({.sql = "SELECT body::text FROM incident.timeline WHERE tenant=$1::uuid AND "
                                                "incident=$2::uuid ORDER BY id DESC LIMIT 1",
                                         .parameters = {Param::text(principal.tenant), Param::text(id)}});
    if (!entry) {
      co_return std::unexpected(entry.error());
    }
    co_return Json{{"entry", Json::parse(entry->value(0, 0).value_or(""))}};
  }
  if (operation == "link") {
    co_return Json{{"link", {{"linkId", ref}, {"url", text}, {"title", body.value("title", "")}}}};
  }
  co_return Json{{"incident", json(value)}};
}

asio::awaitable<p::Result<void>> Repository::tick() {
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
      [this](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto expired = co_await expire_operations(tx);
        if (!expired) {
          co_return expired;
        }
        auto rows = co_await tx->connection().query(
            {.sql =
                 "SELECT tenant_id::text,body::text FROM incident.incidents WHERE "
                 "body->>'state'='INCIDENT_STATE_RESOLVED' AND (body->>'autoCloseAt')::timestamptz<=clock_timestamp() "
                 "ORDER BY incident_id LIMIT 64 FOR UPDATE SKIP LOCKED"});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        for (std::size_t row = 0; row < rows->size(); ++row) {
          auto value = parse<incident::v1::Incident>(Json::parse(rows->value(row, 1).value_or("")));
          if (!value) {
            co_return std::unexpected(value.error());
          }
          value->set_state(incident::v1::INCIDENT_STATE_CLOSED);
          *value->mutable_closed_at() = Time::GetCurrentTime();
          value->clear_auto_close_at();
          value->clear_pending_transfer();
          value->set_version(value->version() + 1);
          auto saved = co_await save(tx, system(std::string(rows->value(row, 0).value_or(""))), *value, "close");
          if (!saved) {
            co_return saved;
          }
        }
        co_return p::Result<void>{};
      });
}

asio::awaitable<p::Result<void>> Repository::apply_response(response::v1::ResponseEventRecord input, Encode outgoing) {
  const auto& origin = input.envelope();
  if (!p::stream::uuid(origin.tenant_id()) || !p::stream::uuid(origin.message_id())) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid response identity");
  }
  if (!input.has_run_updated() && !input.has_escalation_level_triggered() && !input.has_command_updated()
      && !input.has_plan_unavailable() && !input.has_notification_updated() && !input.has_sla_policy_pinned()) {
    co_return p::Result<void>{};
  }
  std::string id;
  if (input.has_sla_policy_pinned()) {
    id = input.sla_policy_pinned().incident_id();
  } else if (input.has_notification_updated()) {
    id = input.notification_updated().notification().incident_id();
  } else if (input.has_command_updated()) {
    id = input.command_updated().command().incident_id();
  } else if (input.has_run_updated()) {
    id = input.run_updated().run().incident_id();
  } else if (input.has_plan_unavailable()) {
    id = input.plan_unavailable().incident_id();
  } else {
    id = input.escalation_level_triggered().incident_id();
  }
  if ((input.has_command_updated() || input.has_notification_updated()) && id.empty()) {
    co_return p::Result<void>{};
  }
  if (!p::stream::uuid(id)) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid response incident");
  }
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
      [this, &input, &id, &outgoing](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        const auto& source = input.envelope();
        auto accepted = co_await messages_.accept(tx, "response", source.tenant_id() + ":" + source.message_id(),
                                                  p::stream::sha256(input.SerializeAsString()));
        if (!accepted) {
          co_return std::unexpected(accepted.error());
        }
        if (!*accepted) {
          co_return p::Result<void>{};
        }
        auto loaded = co_await load(tx, source.tenant_id(), id);
        if (!loaded) {
          co_return std::unexpected(loaded.error());
        }
        auto value = std::move(*loaded);
        if (input.has_sla_policy_pinned()) {
          const auto& pinned = input.sla_policy_pinned();
          if (source.actor().id() != "response-engine" || source.actor().kind() != common::v1::ACTOR_KIND_SERVICE
              || pinned.acknowledge_within().seconds() <= 0 || pinned.resolve_within().seconds() <= 0
              || pinned.acknowledge_within().seconds() > 31536000 || pinned.resolve_within().seconds() > 31536000) {
            co_return p::fail(Code::kProcessingPayloadInvalid, "invalid SLA publisher or targets");
          }
          if (value.operational().has_resolve_due()) {
            co_return p::Result<void>{};
          }
          *value.mutable_operational()->mutable_acknowledge_due() =
              Time::MillisecondsToTimestamp(Time::TimestampToMilliseconds(value.created_at())
                                            + Time::DurationToMilliseconds(pinned.acknowledge_within()));
          *value.mutable_operational()->mutable_resolve_due() =
              Time::MillisecondsToTimestamp(Time::TimestampToMilliseconds(value.created_at())
                                            + Time::DurationToMilliseconds(pinned.resolve_within()));
          value.set_version(value.version() + 1);
          co_return co_await save(tx, system(source.tenant_id()), value, "operational.sla", {}, {}, {},
                                  outgoing ? &outgoing : nullptr, &source);
        }
        if (input.has_plan_unavailable()) {
          if (source.actor().kind() != common::v1::ACTOR_KIND_SERVICE || source.actor().id() != "response-engine") {
            co_return p::fail(Code::kProcessingPayloadInvalid, "invalid plan availability publisher");
          }
          auto types = co_await tx->connection().query(
              {.sql = "SELECT body::text FROM incident.types WHERE tenant=$1::uuid AND id=$2::uuid",
               .parameters = {Param::text(source.tenant_id()), Param::text(value.incident_type_id())}});
          if (!types) {
            co_return std::unexpected(types.error());
          }
          if (!types->size() || !Json::parse(types->value(0, 0).value_or("")).value("responseRequired", false)) {
            co_return p::Result<void>{};
          }
          incident::v1::TimelineEntry entry;
          entry.set_entry_id(source.message_id());
          entry.set_incident_id(id);
          *entry.mutable_at() = source.occurred_at();
          *entry.mutable_actor() = source.actor();
          entry.set_kind(incident::v1::TIMELINE_ENTRY_KIND_COMMENT);
          entry.set_text("Required response plan unavailable");
          (*entry.mutable_details())["warning"] = "response_plan_unavailable";
          auto stored = co_await tx->connection().query(
              {.sql = "INSERT INTO incident.timeline VALUES($1::uuid,$2::uuid,$3::uuid,$4::jsonb)",
               .parameters = {Param::text(source.tenant_id()), Param::text(id), Param::text(entry.entry_id()),
                              Param::text(json(entry).dump())}});
          if (!stored) {
            co_return std::unexpected(stored.error());
          }
          incident::v1::IncidentEventRecord event;
          envelope(event.mutable_envelope(), system(source.tenant_id()), value.version());
          *event.mutable_incident_comment_added()->mutable_incident() = value;
          *event.mutable_incident_comment_added()->mutable_entry() = entry;
          co_return co_await messages_.append(
              tx,
              {event.envelope().message_id(), (outgoing ? outgoing : encode_)("psim.incidents.events.v1", id, event)});
        }
        if (input.has_notification_updated()) {
          const auto& notification = input.notification_updated().notification();
          if (source.actor().kind() != common::v1::ACTOR_KIND_SERVICE || source.actor().id() != "notification-service"
              || !p::stream::uuid(notification.notification_id()) || notification.version() == 0) {
            co_return p::fail(Code::kProcessingPayloadInvalid, "invalid notification publisher");
          }
          auto newer = co_await tx->connection().query(
              {.sql = "INSERT INTO incident.notification_versions VALUES($1::uuid,$2::uuid,$3::bigint) ON CONFLICT DO "
                      "NOTHING RETURNING version",
               .parameters = {Param::text(source.tenant_id()), Param::text(notification.notification_id()),
                              Param::text(std::to_string(notification.version()))}});
          if (!newer) {
            co_return std::unexpected(newer.error());
          }
          if (!newer->size()) {
            co_return p::Result<void>{};
          }
          incident::v1::TimelineEntry entry;
          entry.set_entry_id(source.message_id());
          entry.set_incident_id(id);
          *entry.mutable_at() = source.occurred_at();
          *entry.mutable_actor() = source.actor();
          entry.set_kind(incident::v1::TIMELINE_ENTRY_KIND_NOTIFICATION);
          entry.set_ref_id(notification.notification_id());
          (*entry.mutable_details())["state"] = response::v1::NotificationState_Name(notification.state());
          (*entry.mutable_details())["receipt_id"] = notification.receipt_id();
          (*entry.mutable_details())["failure_code"] = notification.failure_code();
          auto stored = co_await tx->connection().query(
              {.sql = "INSERT INTO incident.timeline VALUES($1::uuid,$2::uuid,$3::uuid,$4::jsonb)",
               .parameters = {Param::text(source.tenant_id()), Param::text(id), Param::text(entry.entry_id()),
                              Param::text(json(entry).dump())}});
          if (!stored) {
            co_return std::unexpected(stored.error());
          }
          incident::v1::IncidentEventRecord event;
          envelope(event.mutable_envelope(), system(source.tenant_id()), value.version());
          *event.mutable_incident_notification_updated()->mutable_incident() = value;
          *event.mutable_incident_notification_updated()->mutable_entry() = entry;
          co_return co_await messages_.append(
              tx,
              {event.envelope().message_id(), (outgoing ? outgoing : encode_)("psim.incidents.events.v1", id, event)});
        }
        if (input.has_command_updated()) {
          const auto& command = input.command_updated().command();
          if (!p::stream::uuid(command.command_id()) || command.version() == 0
              || command.state() < response::v1::COMMAND_STATE_REQUESTED
              || command.state() > response::v1::COMMAND_STATE_UNKNOWN) {
            co_return p::fail(Code::kProcessingPayloadInvalid, "invalid command timeline change");
          }
          auto newer = co_await tx->connection().query(
              {.sql = "INSERT INTO incident.command_versions VALUES($1::uuid,$2::uuid,$3) ON CONFLICT DO NOTHING "
                      "RETURNING version",
               .parameters = {Param::text(source.tenant_id()), Param::text(command.command_id()),
                              Param::integer(static_cast<std::int64_t>(command.version()))}});
          if (!newer) {
            co_return std::unexpected(newer.error());
          }
          if (newer->size() == 0) {
            co_return p::Result<void>{};
          }
          incident::v1::TimelineEntry entry;
          entry.set_entry_id(source.message_id());
          entry.set_incident_id(id);
          *entry.mutable_at() = source.occurred_at();
          *entry.mutable_actor() = source.actor();
          entry.set_kind(incident::v1::TIMELINE_ENTRY_KIND_COMMAND);
          entry.set_ref_id(command.command_id());
          (*entry.mutable_details())["state"] = response::v1::CommandState_Name(command.state());
          (*entry.mutable_details())["version"] = std::to_string(command.version());
          (*entry.mutable_details())["result_code"] = command.result_code();
          auto saved = co_await tx->connection().query(
              {.sql = "INSERT INTO incident.timeline VALUES($1::uuid,$2::uuid,$3::uuid,$4::jsonb)",
               .parameters = {Param::text(source.tenant_id()), Param::text(id), Param::text(entry.entry_id()),
                              Param::text(json(entry).dump())}});
          if (!saved) {
            co_return std::unexpected(saved.error());
          }
          incident::v1::IncidentEventRecord event;
          envelope(event.mutable_envelope(), system(source.tenant_id()), value.version());
          *event.mutable_incident_command_updated()->mutable_incident() = value;
          *event.mutable_incident_command_updated()->mutable_entry() = entry;
          co_return co_await messages_.append(
              tx,
              {event.envelope().message_id(), (outgoing ? outgoing : encode_)("psim.incidents.events.v1", id, event)});
        }
        if (value.state() == incident::v1::INCIDENT_STATE_CLOSED) {
          co_return p::Result<void>{};
        }
        std::string action;
        std::string ref;
        if (input.has_run_updated()) {
          const auto& run = input.run_updated().run();
          if (!p::stream::uuid(run.run_id()) || run.version() == 0 || run.state() < response::v1::RUN_STATE_RUNNING
              || run.state() > response::v1::RUN_STATE_CANCELLED || run.blocking_step_ids_size() > 1000) {
            co_return p::fail(Code::kProcessingPayloadInvalid, "invalid response run");
          }
          auto rows = co_await tx->connection().query(
              {.sql = "SELECT body::text FROM incident.responses WHERE tenant=$1::uuid AND incident=$2::uuid",
               .parameters = {Param::text(source.tenant_id()), Param::text(id)}});
          if (!rows) {
            co_return std::unexpected(rows.error());
          }
          if (rows->size() > 0) {
            auto previous = parse<response::v1::ResponseRun>(Json::parse(rows->value(0, 0).value_or("")));
            if (!previous) {
              co_return std::unexpected(previous.error());
            }
            if (previous->run_id() == run.run_id() && previous->version() >= run.version()) {
              co_return p::Result<void>{};
            }
            if (previous->run_id() != run.run_id() && previous->state() == response::v1::RUN_STATE_RUNNING) {
              co_return p::fail(Code::kProcessingPayloadInvalid, "another response run is active");
            }
          }
          auto stored = co_await tx->connection().query(
              {.sql = "INSERT INTO incident.responses VALUES($1::uuid,$2::uuid,$3::jsonb) ON CONFLICT(tenant,incident) "
                      "DO UPDATE SET body=EXCLUDED.body",
               .parameters = {Param::text(source.tenant_id()), Param::text(id), Param::text(json(run).dump())}});
          if (!stored) {
            co_return std::unexpected(stored.error());
          }
          value.clear_blocking_steps();
          if (run.state() == response::v1::RUN_STATE_RUNNING) {
            for (const auto& step : run.blocking_step_ids()) {
              auto* pending = value.add_blocking_steps();
              pending->set_run_id(run.run_id());
              pending->set_step_id(step);
              pending->set_title(step);
            }
          }
          value.set_response_run_version(run.version());
          action = "blocking";
          ref = run.run_id();
        } else {
          const auto level = input.escalation_level_triggered().level();
          if (level <= value.escalation_level() || value.state() == incident::v1::INCIDENT_STATE_RESOLVED) {
            co_return p::Result<void>{};
          }
          value.set_escalation_level(level);
          action = "escalate";
        }
        value.clear_pending_transfer();
        value.set_version(value.version() + 1);
        co_return co_await save(tx, system(source.tenant_id()), value, action, ref, {}, {},
                                outgoing ? &outgoing : nullptr, &source);
      });
}
}  // namespace psim::incident_service
