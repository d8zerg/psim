// NOLINTBEGIN(cppcoreguidelines-avoid-capturing-lambda-coroutines): in_transaction owns each closure until awaited.
#include "escalation.hpp"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/audit/v1/audit.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "model.hpp"
#include "psim/common/v1/envelope.pb.h"
#include "psim/incident/v1/events.pb.h"
#include "psim/incident/v1/incident.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/platform/timers/store.hpp"
#include "psim/response/v1/events.pb.h"
#include "psim/response/v1/plan.pb.h"

namespace psim::response_engine {
namespace {
using Code = p::ErrorCode;
using Param = p::pg::Parameter;
using Time = google::protobuf::util::TimeUtil;

Json json(const google::protobuf::Message& value) {
  return Json::parse(p::stream::json(value));
}

template <typename T>
T parse(const Json& value) {
  T result;
  if (!google::protobuf::util::JsonStringToMessage(value.dump(), &result).ok()) {
    throw std::invalid_argument("fields");
  }
  return result;
}

void envelope(common::v1::Envelope* value, const std::string& tenant) {
  value->set_message_id(p::stream::uuid7());
  value->set_tenant_id(tenant);
  value->set_schema_version("1");
  value->set_sequence(1);
  value->set_correlation_id(value->message_id());
  *value->mutable_occurred_at() = Time::GetCurrentTime();
  *value->mutable_produced_at() = value->occurred_at();
  value->mutable_actor()->set_id("response-engine");
  value->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
}

asio::awaitable<p::Result<void>> execute(p::pg::Transaction* tx, std::string sql, std::vector<Param> args = {}) {
  auto result = co_await tx->connection().query({.sql = std::move(sql), .parameters = std::move(args)});
  if (!result) {
    co_return std::unexpected(result.error());
  }
  co_return p::Result<void>{};
}

std::int64_t duration(const google::protobuf::Duration& value) {
  return Time::DurationToMilliseconds(value);
}

bool valid_duration(const google::protobuf::Duration& value, bool zero = false) {
  return value.seconds() >= 0 && value.seconds() <= 2592000 && value.nanos() >= 0 && value.nanos() < 1000000000
         && (zero || duration(value) > 0);
}
}  // namespace

p::Result<void> validate_policy(const response::v1::EscalationPolicy& policy) {
  if (!p::stream::uuid(policy.policy_id()) || policy.name().empty() || policy.name().size() > 200
      || policy.sla().empty() || policy.sla_size() > 4 || policy.levels().empty() || policy.levels_size() > 10) {
    return p::fail(Code::kCommonInvalidArgument, "invalid escalation policy");
  }
  std::set<int> priorities;
  std::int64_t previous = -1;
  unsigned int number = 0;
  for (const auto& sla : policy.sla()) {
    if (sla.priority() < 1 || sla.priority() > 4 || !priorities.insert(sla.priority()).second
        || !valid_duration(sla.acknowledge_within()) || !valid_duration(sla.resolve_within())) {
      return p::fail(Code::kCommonInvalidArgument, "invalid SLA target");
    }
  }
  for (const auto& level : policy.levels()) {
    if (level.level() != ++number || !valid_duration(level.delay(), true) || duration(level.delay()) <= previous
        || level.recipients().empty() || level.recipients_size() > 100 || level.channels().empty()) {
      return p::fail(Code::kCommonInvalidArgument, "invalid escalation level");
    }
    previous = duration(level.delay());
    std::set<int> channels;
    for (const int channel : level.channels()) {
      if (channel < 1 || channel > 3 || !channels.insert(channel).second) {
        return p::fail(Code::kCommonInvalidArgument, "invalid channel");
      }
    }
    for (const auto& recipient : level.recipients()) {
      if ((!recipient.user_id().empty() && p::stream::uuid(recipient.user_id())) || recipient.role() == "operator"
          || recipient.role() == "supervisor" || recipient.current_duty()) {
        continue;
      }
      return p::fail(Code::kCommonInvalidArgument, "invalid recipient");
    }
  }
  return {};
}

asio::awaitable<p::Result<void>> Escalation::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  std::vector<std::string> schema = {
      (R"sql(CREATE TABLE escalation.policies(tenant uuid NOT NULL,id uuid NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,id)))sql"),
      (R"sql(CREATE TABLE escalation.duties(tenant uuid NOT NULL,site uuid NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,site)))sql"),
      (R"sql(CREATE TABLE escalation.incidents(tenant uuid NOT NULL,id uuid NOT NULL,version bigint NOT NULL,priority integer NOT NULL,body jsonb NOT NULL,policy jsonb NOT NULL,PRIMARY KEY(tenant,id)))sql"),
      (R"sql(CREATE TABLE escalation.requests(tenant uuid NOT NULL,actor text NOT NULL,key uuid NOT NULL,fingerprint text NOT NULL,result jsonb NOT NULL,PRIMARY KEY(tenant,actor,key)))sql"),
      R"sql(CREATE TABLE escalation.triggers(tenant uuid NOT NULL,incident uuid NOT NULL,kind text NOT NULL,level integer NOT NULL,PRIMARY KEY(tenant,incident,kind,level)))sql"};
  const auto timers = timers_.migration();
  schema.insert(schema.end(), timers.begin(), timers.end());
  co_return co_await p::pg::Migrator("escalation")
      .apply(&**lease, {{.version = 1, .name = "SLA policies and site duties", .expand = std::move(schema)}});
}

asio::awaitable<p::Result<void>> Escalation::publish(p::pg::Transaction* tx, response::v1::ResponseEventRecord event,
                                                     std::string id) {
  co_return co_await messages_.append(
      tx, {event.envelope().message_id(), encode_("psim.response.events.v1", std::move(id), event)});
}

asio::awaitable<p::Result<void>> Escalation::audit(p::pg::Transaction* tx, std::string tenant, std::string actor,
                                                   std::string action, std::string id, std::string site, bool denied) {
  audit::v1::AuditEventRecord event;
  envelope(event.mutable_envelope(), tenant);
  auto* entry = event.mutable_audit_entry();
  entry->mutable_actor()->set_id(actor);
  entry->mutable_actor()->set_kind(actor == "response-engine" ? common::v1::ACTOR_KIND_SERVICE
                                                              : common::v1::ACTOR_KIND_USER);
  entry->set_action(action);
  entry->set_object_type("escalation");
  entry->set_object_id(id);
  entry->set_site_id(site);
  entry->set_outcome(denied ? audit::v1::AUDIT_OUTCOME_DENIED : audit::v1::AUDIT_OUTCOME_SUCCESS);
  co_return co_await messages_.append(tx, {event.envelope().message_id(), encode_("psim.audit.v1", tenant, event)});
}

asio::awaitable<p::Result<Json>> Escalation::request(p::security::Principal principal, std::string operation, Json body,
                                                     std::string key, std::optional<std::uint64_t> expected) {
  const bool duty = operation.starts_with("duty");
  const bool read = operation == "duty.get" || operation == "policy.list";
  const auto site = duty ? body.value("siteId", std::string{}) : std::string{};
  const auto action = duty ? std::string("response.duty.") : std::string("response.policy.");
  auto access = p::authz::decide(principal, action + (read ? "read" : "write"), {principal.tenant, site},
                                 std::chrono::system_clock::now());
  if (!duty && !principal.scopes.contains("*")) {
    access = p::fail(Code::kAuthScopeDenied, "tenant-wide policy scope required");
  }
  if (!access) {
    auto recorded = co_await p::pg::in_transaction<void>(pool_, [&, this](p::pg::Transaction* tx) {
      return audit(tx, principal.tenant, principal.subject, operation, site, site, true);
    });
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
    co_return std::unexpected(access.error());
  }
  if ((duty && !p::stream::uuid(site)) || (!read && !p::stream::uuid(key))) {
    co_return p::fail(Code::kCommonInvalidArgument, "valid identity required");
  }
  co_return co_await p::pg::in_transaction<Json>(
      pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        auto lock = co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                     {Param::text(principal.tenant + ":escalation-config")});
        if (!lock) {
          co_return std::unexpected(lock.error());
        }
        const auto fingerprint = p::stream::sha256(operation + body.dump() + std::to_string(expected.value_or(0)));
        if (!read) {
          auto old = co_await tx->connection().query(
              {.sql = "SELECT fingerprint,result::text FROM escalation.requests WHERE tenant=$1::uuid AND actor=$2 AND "
                      "key=$3::uuid",
               .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key)}});
          if (!old) {
            co_return std::unexpected(old.error());
          }
          if (old->size()) {
            if (old->value(0, 0) != fingerprint) {
              co_return p::fail(Code::kCommonIdempotencyKeyReused, "request key reused");
            }
            co_return Json::parse(old->value(0, 1).value_or(""));
          }
        }
        Json output;
        if (operation == "policy.list") {
          int size = body.value("pageSize", 100);
          if (size == 0) {
            size = 100;
          }
          const auto cursor = body.value("pageToken", std::string{});
          if (size < 1 || size > 1000 || (!cursor.empty() && !p::stream::uuid(cursor))) {
            co_return p::fail(Code::kCommonInvalidArgument, "invalid page");
          }
          auto rows = co_await tx->connection().query(
              {.sql = "SELECT body::text FROM escalation.policies WHERE tenant=$1::uuid AND ($2='' OR "
                      "id>NULLIF($2,'')::uuid) ORDER BY id LIMIT $3::integer",
               .parameters = {Param::text(principal.tenant), Param::text(cursor),
                              Param::text(std::to_string(size + 1))}});
          if (!rows) {
            co_return std::unexpected(rows.error());
          }
          output["policies"] = Json::array();
          for (std::size_t i = 0; i < rows->size(); ++i) {
            if (std::cmp_equal(i, size)) {
              output["nextPageToken"] = output["policies"].back()["policyId"];
              break;
            }
            output["policies"].push_back(Json::parse(rows->value(i, 0).value_or("")));
          }
          co_return output;
        }
        const auto id = duty ? site : body.value("policyId", std::string{});
        if (!p::stream::uuid(id)) {
          co_return p::fail(Code::kCommonInvalidArgument, "invalid identity");
        }
        auto rows = co_await tx->connection().query(
            {.sql = duty ? "SELECT body::text FROM escalation.duties WHERE tenant=$1::uuid AND site=$2::uuid"
                         : "SELECT body::text FROM escalation.policies WHERE tenant=$1::uuid AND id=$2::uuid",
             .parameters = {Param::text(principal.tenant), Param::text(id)}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        if (read) {
          if (rows->size() == 0) {
            co_return p::fail(Code::kCommonNotFound, "duty unavailable");
          }
          co_return Json{{"duty", Json::parse(rows->value(0, 0).value_or(""))}};
        }
        const auto version =
            rows->size() ? std::stoull(Json::parse(rows->value(0, 0).value_or("")).value("version", std::string("0")))
                         : 0;
        if (operation == "policy.create" && version) {
          co_return p::fail(Code::kCommonConcurrentModification, "policy exists");
        }
        if (operation == "policy.update" && !version) {
          co_return p::fail(Code::kCommonNotFound, "policy unavailable");
        }
        if (version && !expected) {
          co_return p::fail(Code::kCommonPreconditionRequired, "If-Match required");
        }
        if (expected && *expected != version) {
          co_return p::fail(Code::kCommonConcurrentModification, "version changed");
        }
        if (duty) {
          const auto user = body.value("userId", std::string{});
          const auto fallback = body.value("fallbackUserId", std::string{});
          if (!p::stream::uuid(fallback) || (!user.empty() && !p::stream::uuid(user))) {
            co_return p::fail(Code::kCommonInvalidArgument, "invalid duty recipients");
          }
          for (const auto& subject : {user, fallback}) {
            if (subject.empty()) {
              continue;
            }
            if (!lookup_) {
              co_return p::fail(Code::kCommonUnavailable, "directory unavailable");
            }
            auto person = co_await lookup_(subject);
            if (!person) {
              co_return std::unexpected(person.error());
            }
            if (!p::authz::scoped(*person, {principal.tenant, site})
                || (!person->roles.contains("operator") && !person->roles.contains("supervisor"))) {
              co_return p::fail(Code::kIncidentAssigneeInvalid, "duty recipient outside scope");
            }
          }
          body["version"] = std::to_string(version + 1);
          output["duty"] = body;
        } else {
          auto policy = parse<response::v1::EscalationPolicy>(body);
          auto valid = validate_policy(policy);
          if (!valid) {
            co_return std::unexpected(valid.error());
          }
          policy.set_version(version + 1);
          body = json(policy);
          output["policy"] = body;
        }
        auto stored = co_await execute(tx,
                                       duty ? "INSERT INTO escalation.duties VALUES($1::uuid,$2::uuid,$3::jsonb) ON "
                                              "CONFLICT(tenant,site) DO UPDATE SET body=EXCLUDED.body"
                                            : "INSERT INTO escalation.policies VALUES($1::uuid,$2::uuid,$3::jsonb) ON "
                                              "CONFLICT(tenant,id) DO UPDATE SET body=EXCLUDED.body",
                                       {Param::text(principal.tenant), Param::text(id), Param::text(body.dump())});
        if (!stored) {
          co_return std::unexpected(stored.error());
        }
        auto logged = co_await audit(tx, principal.tenant, principal.subject, operation, id, site);
        if (!logged) {
          co_return std::unexpected(logged.error());
        }
        auto cached = co_await execute(tx, "INSERT INTO escalation.requests VALUES($1::uuid,$2,$3::uuid,$4,$5::jsonb)",
                                       {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key),
                                        Param::text(fingerprint), Param::text(output.dump())});
        if (!cached) {
          co_return std::unexpected(cached.error());
        }
        co_return output;
      });
}

asio::awaitable<p::Result<void>> Escalation::incident(incident::v1::IncidentEventRecord input) {
  const auto* field = incident::v1::IncidentEventRecord::GetReflection()->GetOneofFieldDescriptor(
      input, incident::v1::IncidentEventRecord::GetDescriptor()->oneof_decl(0));
  if (!field) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "missing incident payload");
  }
  const auto& payload = incident::v1::IncidentEventRecord::GetReflection()->GetMessage(input, field);
  const auto* snapshot = payload.GetDescriptor()->FindFieldByName("incident");
  if (!snapshot) {
    co_return p::Result<void>{};
  }
  auto value = parse<incident::v1::Incident>(json(payload.GetReflection()->GetMessage(payload, snapshot)));
  const auto tenant = input.envelope().tenant_id();
  if (!p::stream::uuid(tenant) || !p::stream::uuid(value.incident_id()) || !p::stream::uuid(value.site_id())
      || value.version() == 0 || !value.has_created_at()) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid SLA incident");
  }
  if (value.escalation_policy_id().empty()) {
    co_return p::Result<void>{};
  }
  if (!p::stream::uuid(value.escalation_policy_id())) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid policy identity");
  }
  co_return co_await p::pg::in_transaction<void>(
      pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto lock = co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                     {Param::text(tenant + ":sla:" + value.incident_id())});
        if (!lock) {
          co_return lock;
        }
        auto old =
            co_await tx->connection().query({.sql = "SELECT version,policy::text,priority FROM escalation.incidents "
                                                    "WHERE tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
                                             .parameters = {Param::text(tenant), Param::text(value.incident_id())}});
        if (!old) {
          co_return std::unexpected(old.error());
        }
        Json policy;
        int priority = static_cast<int>(value.priority());
        if (old->size()) {
          if (std::stoull(std::string(old->value(0, 0).value_or(""))) >= value.version()) {
            co_return p::Result<void>{};
          }
          policy = Json::parse(old->value(0, 1).value_or(""));
          priority = std::stoi(std::string(old->value(0, 2).value_or("")));
        } else {
          auto selected = co_await tx->connection().query(
              {.sql = "SELECT body::text FROM escalation.policies WHERE tenant=$1::uuid AND id=$2::uuid",
               .parameters = {Param::text(tenant), Param::text(value.escalation_policy_id())}});
          if (!selected) {
            co_return std::unexpected(selected.error());
          }
          if (selected->size() == 0) {
            co_return p::fail(Code::kCommonUnavailable, "referenced SLA policy unavailable");
          }
          policy = Json::parse(selected->value(0, 0).value_or(""));
        }
        auto stored = co_await execute(
            tx,
            ("INSERT INTO escalation.incidents VALUES($1::uuid,$2::uuid,$3::bigint,$4::integer,$5::jsonb,$6::jsonb) ON "
             "CONFLICT(tenant,id) DO UPDATE SET version=EXCLUDED.version,body=EXCLUDED.body"),
            {Param::text(tenant), Param::text(value.incident_id()), Param::text(std::to_string(value.version())),
             Param::text(std::to_string(priority)), Param::text(json(value).dump()), Param::text(policy.dump())});
        if (!stored) {
          co_return stored;
        }
        const auto definition = parse<response::v1::EscalationPolicy>(policy);
        const auto target = std::ranges::find(definition.sla(), priority, &response::v1::SlaTarget::priority);
        if (target == definition.sla().end()) {
          co_return p::fail(Code::kCommonUnavailable, "priority missing in SLA policy");
        }
        if (old->size() == 0) {
          response::v1::ResponseEventRecord pinned;
          envelope(pinned.mutable_envelope(), tenant);
          auto* targets = pinned.mutable_sla_policy_pinned();
          targets->set_incident_id(value.incident_id());
          *targets->mutable_created_at() = value.created_at();
          *targets->mutable_acknowledge_within() = target->acknowledge_within();
          *targets->mutable_resolve_within() = target->resolve_within();
          auto published = co_await publish(tx, std::move(pinned), value.incident_id());
          if (!published) {
            co_return published;
          }
        }
        // Deadline identity fixes its first timestamp; assignment and priority changes cannot postpone it.
        for (const std::string kind : {"ack", "resolve"}) {
          if (kind == "resolve" && !value.has_acknowledged_at()) {
            continue;
          }
          const auto base = kind == "ack" ? Time::TimestampToMilliseconds(value.created_at())
                                          : Time::TimestampToMilliseconds(value.acknowledged_at());
          const auto due = base + duration(kind == "ack" ? target->acknowledge_within() : target->resolve_within());
          auto saved = co_await timers_.schedule(
              tx, {tenant, value.incident_id(), value.incident_id() + ":" + kind + ":0", due});
          if (!saved) {
            co_return saved;
          }
          for (const auto& level : definition.levels()) {
            saved =
                co_await timers_.schedule(tx, {tenant, value.incident_id(),
                                               value.incident_id() + ":" + kind + ":" + std::to_string(level.level()),
                                               due + duration(level.delay())});
            if (!saved) {
              co_return saved;
            }
          }
        }
        co_return p::Result<void>{};
      });
}

asio::awaitable<p::Result<void>> Escalation::trigger(p::pg::Transaction* tx, std::string tenant,
                                                     incident::v1::Incident value,
                                                     response::v1::EscalationPolicy policy,
                                                     response::v1::EscalationLevel level, std::string kind) {
  auto inserted = co_await tx->connection().query(
      {.sql = "INSERT INTO escalation.triggers VALUES($1::uuid,$2::uuid,$3,$4::integer) ON CONFLICT DO NOTHING "
              "RETURNING level",
       .parameters = {Param::text(tenant), Param::text(value.incident_id()), Param::text(kind),
                      Param::text(std::to_string(level.level()))}});
  if (!inserted) {
    co_return std::unexpected(inserted.error());
  }
  if (inserted->size() == 0) {
    co_return p::Result<void>{};
  }
  response::v1::ResponseEventRecord requested;
  envelope(requested.mutable_envelope(), tenant);
  auto* request = requested.mutable_notification_requested();
  request->set_request_id(p::stream::uuid5(value.incident_id(), {kind, std::to_string(level.level())}));
  request->set_incident_id(value.incident_id());
  request->set_site_id(value.site_id());
  request->set_zone_id(value.zone_id());
  request->mutable_params()->set_template_("SLA " + kind + " escalation " + std::to_string(level.level()));
  *request->mutable_params()->mutable_channels() = level.channels();
  for (const auto& recipient : level.recipients()) {
    if (!recipient.current_duty()) {
      *request->mutable_params()->add_recipients() = recipient;
      continue;
    }
    auto rows = co_await tx->connection().query(
        {.sql = "SELECT body::text FROM escalation.duties WHERE tenant=$1::uuid AND site=$2::uuid",
         .parameters = {Param::text(tenant), Param::text(value.site_id())}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    if (rows->size() == 0) {
      co_return p::fail(Code::kCommonUnavailable, "site duty and fallback not configured");
    }
    const auto duty = Json::parse(rows->value(0, 0).value_or(""));
    auto user = duty.value("userId", std::string{});
    bool missing = user.empty();
    if (!user.empty()) {
      if (!lookup_) {
        co_return p::fail(Code::kCommonUnavailable, "directory unavailable");
      }
      auto person = co_await lookup_(user);
      if (!person && person.error().code() == Code::kCommonUnavailable) {
        co_return std::unexpected(person.error());
      }
      missing = !person || !p::authz::scoped(*person, {tenant, value.site_id()})
                || (!person->roles.contains("operator") && !person->roles.contains("supervisor"));
    }
    if (missing) {
      user = duty.at("fallbackUserId").get<std::string>();
      request->mutable_params()->set_template_(request->params().template_()
                                               + "; current duty unavailable; fallback selected");
      auto logged =
          co_await audit(tx, tenant, "response-engine", "response.duty.missing", value.incident_id(), value.site_id());
      if (!logged) {
        co_return logged;
      }
    }
    request->mutable_params()->add_recipients()->set_user_id(user);
  }
  auto published = co_await publish(tx, requested, value.incident_id());
  if (!published) {
    co_return published;
  }
  response::v1::ResponseEventRecord event;
  envelope(event.mutable_envelope(), tenant);
  auto* fired = event.mutable_escalation_level_triggered();
  fired->set_incident_id(value.incident_id());
  fired->set_level(level.level());
  fired->set_policy_id(policy.policy_id());
  fired->set_sla_kind(kind);
  // Fan-out notification identities are deterministically derived from this request by its owner.
  auto emitted = co_await publish(tx, std::move(event), value.incident_id());
  if (!emitted) {
    co_return emitted;
  }
  co_return co_await audit(tx, tenant, "response-engine", "response.escalation." + kind, value.incident_id(),
                           value.site_id());
}

asio::awaitable<p::Result<void>> Escalation::tick() {
  p::Result<std::vector<p::timers::Timer>> due;
  {
    auto lease = co_await pool_->acquire();
    if (!lease) {
      co_return std::unexpected(lease.error());
    }
    due = co_await timers_.due(&**lease);
  }
  if (!due) {
    co_return std::unexpected(due.error());
  }
  p::Result<void> deferred;
  for (const auto& timer : *due) {
    auto applied = co_await p::pg::in_transaction<void>(
        pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          auto lock = co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                       {Param::text(timer.tenant + ":sla:" + timer.owner)});
          if (!lock) {
            co_return lock;
          }
          auto rows =
              co_await tx->connection().query({.sql = "SELECT body::text,policy::text FROM escalation.incidents WHERE "
                                                      "tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
                                               .parameters = {Param::text(timer.tenant), Param::text(timer.owner)}});
          if (!rows) {
            co_return std::unexpected(rows.error());
          }
          auto fired = co_await timers_.fire(tx, timer);
          if (!fired) {
            co_return std::unexpected(fired.error());
          }
          if (!*fired || !rows->size()) {
            co_return p::Result<void>{};
          }
          const auto value = parse<incident::v1::Incident>(Json::parse(rows->value(0, 0).value_or("")));
          const auto policy = parse<response::v1::EscalationPolicy>(Json::parse(rows->value(0, 1).value_or("")));
          const auto colon = timer.id.rfind(':');
          const auto kind = timer.id.substr(timer.owner.size() + 1, colon - timer.owner.size() - 1);
          const auto level = std::stoul(timer.id.substr(colon + 1));
          const bool complete = kind == "ack" ? value.has_acknowledged_at() : value.has_resolved_at();
          const auto completed = kind == "ack" ? value.acknowledged_at() : value.resolved_at();
          if (level == 0) {
            if (complete && Time::TimestampToMilliseconds(completed) <= timer.due_ms) {
              co_return p::Result<void>{};
            }
            response::v1::ResponseEventRecord event;
            envelope(event.mutable_envelope(), timer.tenant);
            auto* breach = event.mutable_sla_breached();
            breach->set_incident_id(timer.owner);
            breach->set_policy_id(policy.policy_id());
            breach->set_sla_kind(kind);
            auto published = co_await publish(tx, std::move(event), timer.owner);
            if (!published) {
              co_return published;
            }
            co_return co_await audit(tx, timer.tenant, "response-engine", "response.sla.breached." + kind, timer.owner,
                                     value.site_id());
          }
          if (complete || value.state() == incident::v1::INCIDENT_STATE_CLOSED) {
            co_return p::Result<void>{};
          }
          const auto selected = std::ranges::find(policy.levels(), level, &response::v1::EscalationLevel::level);
          if (selected == policy.levels().end()) {
            co_return p::fail(Code::kProcessingPayloadInvalid, "invalid pinned level");
          }
          co_return co_await trigger(tx, timer.tenant, value, policy, *selected, kind);
        });
    if (!applied) {
      deferred = std::unexpected(applied.error());
    }
  }
  co_return deferred;
}

asio::awaitable<p::Result<void>> Escalation::response(response::v1::ResponseEventRecord input) {
  if (!input.has_escalation_level_triggered() || !input.escalation_level_triggered().policy_id().empty()) {
    co_return p::Result<void>{};
  }
  const auto& fired = input.escalation_level_triggered();
  const auto tenant = input.envelope().tenant_id();
  if (!p::stream::uuid(tenant) || !p::stream::uuid(fired.incident_id())
      || input.envelope().actor().id() != "response-engine") {
    co_return p::fail(Code::kProcessingPayloadInvalid, "untrusted escalation");
  }
  co_return co_await p::pg::in_transaction<void>(
      pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto rows =
            co_await tx->connection().query({.sql = "SELECT body::text,policy::text FROM escalation.incidents WHERE "
                                                    "tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
                                             .parameters = {Param::text(tenant), Param::text(fired.incident_id())}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        if (rows->size() == 0) {
          co_return p::Result<void>{};
        }
        const auto value = parse<incident::v1::Incident>(Json::parse(rows->value(0, 0).value_or("")));
        const auto policy = parse<response::v1::EscalationPolicy>(Json::parse(rows->value(0, 1).value_or("")));
        const auto selected = std::ranges::find(policy.levels(), fired.level(), &response::v1::EscalationLevel::level);
        if (selected == policy.levels().end()) {
          co_return p::Result<void>{};
        }
        co_return co_await trigger(tx, tenant, value, policy, *selected, "manual");
      });
}
}  // namespace psim::response_engine

// NOLINTEND(cppcoreguidelines-avoid-capturing-lambda-coroutines)
