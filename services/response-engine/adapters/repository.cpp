#include "repository.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/incident/v1/events.pb.h>
#include <psim/incident/v1/incident.pb.h>
#include <psim/response/v1/command.pb.h>
#include <psim/response/v1/events.pb.h>
#include <psim/response/v1/plan.pb.h>
#include <psim/response/v1/run.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "model.hpp"
#include "psim/domain/response/plan.hpp"
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

namespace psim::response_engine {
namespace pg = p::pg;
using Param = pg::Parameter;
using Code = p::ErrorCode;
using Time = google::protobuf::util::TimeUtil;

namespace {
std::int64_t now() {
  return Time::TimestampToMilliseconds(Time::GetCurrentTime());
}

Json json(const google::protobuf::Message& value) {
  return Json::parse(p::stream::json(value));
}

template <typename T>
p::Result<T> parse(const Json& body) {
  T value;
  if (!google::protobuf::util::JsonStringToMessage(body.dump(), &value).ok()) {
    return p::fail(Code::kCommonInvalidArgument, "invalid fields");
  }
  return value;
}

void envelope(common::v1::Envelope* output, const std::string& tenant, const std::string& actor,
              std::uint64_t version = 1) {
  output->set_message_id(p::stream::uuid7());
  output->set_tenant_id(tenant);
  output->set_sequence(version);
  output->set_schema_version("1");
  output->set_correlation_id(output->message_id());
  *output->mutable_occurred_at() = Time::GetCurrentTime();
  *output->mutable_produced_at() = output->occurred_at();
  output->mutable_actor()->set_id(actor);
  output->mutable_actor()->set_kind(actor == "response-engine" ? common::v1::ACTOR_KIND_SERVICE
                                                               : common::v1::ACTOR_KIND_USER);
}

asio::awaitable<p::Result<void>> execute(pg::Transaction* tx, std::string sql, std::vector<Param> parameters = {}) {
  auto rows = co_await tx->connection().query({.sql = std::move(sql), .parameters = std::move(parameters)});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  co_return p::Result<void>{};
}

p::Result<void> allowed(const p::security::Principal& principal, const std::string& action, const Value& value) {
  return p::authz::decide(principal, action, {value.tenant, value.site, value.zone}, std::chrono::system_clock::now());
}

p::Result<std::vector<d::Change>> advance(const Value& value, d::Operation operation, d::Result result = {},
                                          bool supervisor = false) {
  auto plan = model(value.plan);
  if (!plan) {
    return std::unexpected(plan.error());
  }
  auto changes = d::advance(*plan, model(value.run), value.run.active_step_ids(0), operation, std::move(result),
                            supervisor, now());
  if (!changes) {
    return p::fail(Code::kResponseStepNotActive, "invalid step transition or result");
  }
  return std::move(*changes);
}

const response::v1::Step& active(const Value& value) {
  return *std::ranges::find(value.plan.steps(), value.run.active_step_ids(0), &response::v1::Step::step_id);
}

response::v1::StepExecution* execution(Value* value) {
  return &*std::ranges::find(*value->run.mutable_steps(), value->run.active_step_ids(0),
                             &response::v1::StepExecution::step_id);
}
}  // namespace

asio::awaitable<p::Result<void>> Repository::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto initialized = co_await messages_.initialize(&**lease);
  if (!initialized) {
    co_return initialized;
  }
  std::vector<std::string> schema = {
      ("CREATE TABLE response.plans(tenant uuid NOT NULL,id uuid NOT NULL,version integer NOT NULL,state integer NOT "
       "NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,id,version))"),
      ("CREATE TABLE response.runs(tenant uuid NOT NULL,id uuid NOT NULL,incident uuid NOT NULL,site text NOT "
       "NULL,zone "
       "text NOT NULL,plan jsonb NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,id),UNIQUE(tenant,incident))"),
      ("CREATE TABLE response.resources(tenant uuid NOT NULL,kind text NOT NULL,id uuid NOT NULL,version bigint NOT "
       "NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,kind,id))"),
      ("CREATE TABLE response.requests(tenant uuid NOT NULL,actor text NOT NULL,key uuid NOT NULL,fingerprint text "
       "NOT NULL,result jsonb NOT NULL,PRIMARY KEY(tenant,actor,key))"),
      ("CREATE TABLE response.command_results(tenant uuid NOT NULL,id uuid NOT NULL,version bigint NOT NULL,state "
       "integer NOT NULL,PRIMARY KEY(tenant,id))"),
      ("CREATE TABLE response.effects(tenant uuid NOT NULL,run uuid NOT NULL,id uuid NOT NULL,step text NOT "
       "NULL,attempt integer NOT NULL,url text NOT NULL,timeout integer NOT NULL,status text NOT NULL,PRIMARY "
       "KEY(tenant,id))"),
      ("CREATE INDEX effects_pending ON response.effects(status) WHERE status='pending'")};
  const auto timers = timers_.migration();
  schema.insert(schema.end(), timers.begin(), timers.end());
  co_return co_await pg::Migrator("response")
      .apply(&**lease, {{.version = 1, .name = "durable response execution", .expand = std::move(schema)}});
}

asio::awaitable<p::Result<void>> Repository::audit(pg::Transaction* tx, std::string tenant, std::string actor,
                                                   std::string action, std::string id, std::string site, bool denied) {
  audit::v1::AuditEventRecord event;
  envelope(event.mutable_envelope(), tenant, actor);
  auto* entry = event.mutable_audit_entry();
  *entry->mutable_actor() = event.envelope().actor();
  entry->set_action(denied ? "access.denied" : action);
  if (denied) {
    (*entry->mutable_details())["requested_action"] = std::move(action);
  }
  entry->set_object_type("response");
  entry->set_object_id(std::move(id));
  entry->set_site_id(std::move(site));
  entry->set_outcome(denied ? audit::v1::AUDIT_OUTCOME_DENIED : audit::v1::AUDIT_OUTCOME_SUCCESS);
  co_return co_await messages_.append(tx, {event.envelope().message_id(), encode_("psim.audit.v1", tenant, event)});
}

asio::awaitable<p::Result<void>> Repository::emit(pg::Transaction* tx, response::v1::ResponseEventRecord event) {
  std::string id;
  if (event.has_run_updated()) {
    id = event.run_updated().run().incident_id();
  } else if (event.has_command_requested()) {
    id = event.command_requested().command().incident_id();
  } else if (event.has_notification_requested()) {
    id = event.notification_requested().incident_id();
  } else if (event.has_plan_unavailable()) {
    id = event.plan_unavailable().incident_id();
  } else {
    id = event.escalation_level_triggered().incident_id();
  }
  co_return co_await messages_.append(tx,
                                      {event.envelope().message_id(), encode_("psim.response.events.v1", id, event)});
}

asio::awaitable<p::Result<Value>> Repository::load(pg::Transaction* tx, std::string tenant, std::string id) {
  auto rows = co_await tx->connection().query(
      {.sql =
           "SELECT site,zone,plan::text,body::text FROM response.runs WHERE tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
       .parameters = {Param::text(tenant), Param::text(id)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0) {
    co_return p::fail(Code::kCommonNotFound, "run unavailable");
  }
  auto plan = parse<response::v1::ResponsePlan>(Json::parse(rows->value(0, 2).value_or("")));
  auto run = parse<response::v1::ResponseRun>(Json::parse(rows->value(0, 3).value_or("")));
  if (!plan) {
    co_return std::unexpected(plan.error());
  }
  if (!run) {
    co_return std::unexpected(run.error());
  }
  co_return Value{tenant, std::string(rows->value(0, 0).value_or("")), std::string(rows->value(0, 1).value_or("")),
                  *plan, *run};
}

asio::awaitable<p::Result<void>> Repository::save(pg::Transaction* tx, Value* value, std::vector<d::Change> changes,
                                                  std::string actor) {
  for (const auto& change : changes) {
    value->run = snapshot(change.run, value->run, value->plan, actor);
    response::v1::ResponseEventRecord event;
    envelope(event.mutable_envelope(), value->tenant, actor, value->run.version());
    event.mutable_run_updated()->set_change(static_cast<response::v1::RunChange>(change.kind));
    event.mutable_run_updated()->set_step_id(change.step);
    *event.mutable_run_updated()->mutable_run() = value->run;
    auto appended = co_await emit(tx, std::move(event));
    if (!appended) {
      co_return appended;
    }
    auto recorded = co_await audit(tx, value->tenant, actor, "response.change." + std::to_string(change.kind),
                                   value->run.run_id(), value->site);
    if (!recorded) {
      co_return recorded;
    }
  }
  auto stored =
      co_await execute(tx,
                       "INSERT INTO response.runs VALUES($1::uuid,$2::uuid,$3::uuid,$4,$5,$6::jsonb,$7::jsonb) ON "
                       "CONFLICT(tenant,id) DO "
                       "UPDATE SET body=EXCLUDED.body",
                       {Param::text(value->tenant), Param::text(value->run.run_id()),
                        Param::text(value->run.incident_id()), Param::text(value->site), Param::text(value->zone),
                        Param::text(json(value->plan).dump()), Param::text(json(value->run).dump())});
  if (!stored) {
    co_return stored;
  }
  if (value->run.state() != response::v1::RUN_STATE_RUNNING) {
    auto cancelled = co_await timers_.cancel(tx, value->tenant, value->run.run_id());
    if (!cancelled) {
      co_return cancelled;
    }
    co_return co_await execute(tx,
                               "UPDATE response.effects SET status='cancelled' WHERE tenant=$1::uuid AND "
                               "run=$2::uuid AND status='pending'",
                               {Param::text(value->tenant), Param::text(value->run.run_id())});
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::finish(pg::Transaction* tx, Value* value, d::Operation operation,
                                                    std::string note) {
  auto changes = advance(*value, operation, {.note = std::move(note)});
  if (!changes) {
    co_return std::unexpected(changes.error());
  }
  co_return co_await save(tx, value, std::move(*changes), "response-engine");
}

asio::awaitable<p::Result<void>> Repository::drive(pg::Transaction* tx, Value* value,
                                                   std::optional<p::security::Principal> manual) {
  for (int limit = 0; limit < 200 && value->run.state() == response::v1::RUN_STATE_RUNNING; ++limit) {
    const auto& step = active(*value);
    auto* current = execution(value);
    if (current->state() != response::v1::STEP_STATE_ACTIVE) {
      break;
    }
    const auto id = effect_id(value->run, step.step_id(), current->attempt());
    if (step.has_wait()) {
      co_return co_await timers_.schedule(tx, {value->tenant, value->run.run_id(), id,
                                               Time::TimestampToMilliseconds(current->activated_at())
                                                   + Time::DurationToMilliseconds(step.wait().duration())});
    }
    if (step.has_automated()) {
      co_return co_await execute(
          tx,
          "INSERT INTO response.effects VALUES($1::uuid,$2::uuid,$3::uuid,$4,$5::integer,$6,"
          "$7::integer,'pending') ON CONFLICT DO NOTHING",
          {Param::text(value->tenant), Param::text(value->run.run_id()), Param::text(id), Param::text(step.step_id()),
           Param::text(std::to_string(current->attempt())), Param::text(step.automated().webhook_url()),
           Param::text(std::to_string(step.automated().timeout().seconds()))});
    }
    if (step.has_command()) {
      if ((!step.command().automatic() && !manual) || !current->result().command_ids().empty()) {
        break;
      }
      auto targets = co_await tx->connection().query(
          {.sql = "SELECT body::text FROM response.resources WHERE tenant=$1::uuid AND kind='device' AND "
                  "(($2<>'' AND id=NULLIF($2,'')::uuid) OR ($2='' AND body->>'siteId'=$3 AND body->>'zoneId'=$4)) "
                  "ORDER BY id LIMIT 129",
           .parameters = {Param::text(value->tenant), Param::text(step.command().device_id()), Param::text(value->site),
                          Param::text(value->zone)}});
      if (!targets) {
        co_return std::unexpected(targets.error());
      }
      if (targets->size() == 0 || targets->size() > 128) {
        co_return co_await finish(tx, value, d::Operation::kFailure, "command target count outside 1..128");
      }
      for (std::size_t index = 0; index < targets->size(); ++index) {
        const auto device = Json::parse(targets->value(index, 0).value_or(""));
        if (device.value("siteId", std::string{}) != value->site
            || device.value("zoneId", std::string{}) != value->zone) {
          co_return co_await finish(tx, value, d::Operation::kFailure, "command target outside incident zone");
        }
        const auto target = device.at("deviceId").get<std::string>();
        const auto command_id = effect_id(value->run, step.step_id(), current->attempt(), target);
        current->mutable_result()->add_command_ids(command_id);
        response::v1::ResponseEventRecord event;
        envelope(event.mutable_envelope(), value->tenant, "response-engine");
        auto* request = event.mutable_command_requested();
        request->set_site_id(value->site);
        request->set_zone_id(value->zone);
        auto* command = request->mutable_command();
        command->set_command_id(command_id);
        command->mutable_requested_by()->set_id(manual ? manual->subject : "response-engine");
        command->mutable_requested_by()->set_kind(manual ? common::v1::ACTOR_KIND_USER
                                                         : common::v1::ACTOR_KIND_SERVICE);
        if (manual) {
          *current->mutable_actor() = command->requested_by();
        }
        command->set_incident_id(value->run.incident_id());
        command->set_run_id(value->run.run_id());
        command->set_step_id(step.step_id());
        command->set_device_id(target);
        command->set_capability(step.command().capability());
        *command->mutable_parameters() = step.command().parameters();
        auto emitted = co_await emit(tx, std::move(event));
        if (!emitted) {
          co_return emitted;
        }
      }
      auto domain = model(value->run);
      ++domain.version;
      co_return co_await save(tx, value, {{2, step.step_id(), domain}}, "response-engine");
    }
    if (step.has_notify() || step.has_escalate()) {
      response::v1::ResponseEventRecord event;
      envelope(event.mutable_envelope(), value->tenant, "response-engine");
      if (step.has_notify()) {
        auto* request = event.mutable_notification_requested();
        request->set_request_id(id);
        request->set_incident_id(value->run.incident_id());
        request->set_run_id(value->run.run_id());
        request->set_step_id(step.step_id());
        request->set_site_id(value->site);
        request->set_zone_id(value->zone);
        *request->mutable_params() = step.notify();
      } else {
        auto* request = event.mutable_escalation_level_triggered();
        request->set_incident_id(value->run.incident_id());
        request->set_level(step.escalate().level());
        request->set_policy_id(value->run.plan_id());
      }
      auto emitted = co_await emit(tx, std::move(event));
      if (!emitted) {
        co_return emitted;
      }
      auto completed = co_await finish(tx, value, d::Operation::kSuccess);
      if (!completed) {
        co_return completed;
      }
      manual.reset();
      continue;
    }
    break;
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::catalog(catalog::v1::CatalogRecord input) {
  const auto tenant = input.envelope().tenant_id();
  std::string kind;
  std::string id;
  std::uint64_t version = 0;
  Json body;
  if (input.has_device_type()) {
    kind = "type";
    id = input.device_type().device_type_id();
    version = input.device_type().version();
    body = json(input.device_type());
  } else if (input.has_device()) {
    kind = "device";
    id = input.device().device_id();
    version = input.device().version();
    body = json(input.device());
  } else {
    co_return p::Result<void>{};
  }
  if (!p::stream::uuid(tenant) || !p::stream::uuid(id) || version == 0) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid catalog identity or version");
  }
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
      [&](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        co_return co_await execute(tx,
                                   "INSERT INTO response.resources VALUES($1::uuid,$2,$3::uuid,$4::bigint,$5::jsonb) "
                                   "ON CONFLICT(tenant,kind,id) "
                                   "DO UPDATE SET version=EXCLUDED.version,body=EXCLUDED.body WHERE "
                                   "response.resources.version<EXCLUDED.version",
                                   {Param::text(tenant), Param::text(kind), Param::text(id),
                                    Param::text(std::to_string(version)), Param::text(body.dump())});
      });
}

asio::awaitable<p::Result<void>> Repository::validate_catalog(pg::Transaction* tx, std::string tenant,
                                                              response::v1::ResponsePlan plan) {
  auto valid = model(plan);
  if (!valid) {
    co_return std::unexpected(valid.error());
  }
  auto rows = co_await tx->connection().query({.sql = "SELECT body::text FROM response.resources WHERE "
                                                      "tenant=$1::uuid AND kind='type' LIMIT 10000",
                                               .parameters = {Param::text(tenant)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  std::set<std::string> capabilities;
  for (std::size_t index = 0; index < rows->size(); ++index) {
    const auto type = Json::parse(rows->value(index, 0).value_or(""));
    if (type.value("state", std::string{}) == "RESOURCE_STATE_ACTIVE") {
      for (const auto& capability : type.value("capabilities", Json::array())) {
        capabilities.insert(capability.at("code").get<std::string>());
      }
    }
  }
  for (const auto& step : plan.steps()) {
    if (step.has_command() && !capabilities.contains(step.command().capability())) {
      co_return p::fail(Code::kCommandCapabilityUnsupported, "capability absent from active device types");
    }
    if (step.has_automated() && !handlers_.contains(step.automated().webhook_url())) {
      co_return p::fail(Code::kCommonInvalidArgument, "handler outside configured allowlist");
    }
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::incident(incident::v1::IncidentEventRecord input) {
  const auto& origin = input.envelope();
  const auto* field = psim::incident::v1::IncidentEventRecord::GetReflection()->GetOneofFieldDescriptor(
      input, psim::incident::v1::IncidentEventRecord::GetDescriptor()->oneof_decl(0));
  if (!field || !p::stream::uuid(origin.tenant_id()) || !p::stream::uuid(origin.message_id())) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid incident event");
  }
  const auto& payload = psim::incident::v1::IncidentEventRecord::GetReflection()->GetMessage(input, field);
  const auto* incident_field = payload.GetDescriptor()->FindFieldByName("incident");
  if (!incident_field) {
    co_return p::Result<void>{};
  }
  auto incident = parse<incident::v1::Incident>(json(payload.GetReflection()->GetMessage(payload, incident_field)));
  if (!incident || !p::stream::uuid(incident->incident_id()) || !p::stream::uuid(incident->site_id())
      || incident->version() == 0) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid incident snapshot");
  }
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
      [&, this](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto lock = co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                     {Param::text(origin.tenant_id() + ":incident:" + incident->incident_id())});
        if (!lock) {
          co_return lock;
        }
        auto accepted = co_await messages_.accept(tx, "incident", origin.message_id(),
                                                  p::stream::sha256(input.SerializeAsString()));
        if (!accepted) {
          co_return std::unexpected(accepted.error());
        }
        if (!*accepted) {
          co_return p::Result<void>{};
        }
        auto updated = co_await execute(
            tx,
            "INSERT INTO response.resources VALUES($1::uuid,'incident',$2::uuid,$3::bigint,$4::jsonb) ON "
            "CONFLICT(tenant,kind,id) DO UPDATE SET version=EXCLUDED.version,body=EXCLUDED.body WHERE "
            "response.resources.version<EXCLUDED.version",
            {Param::text(origin.tenant_id()), Param::text(incident->incident_id()),
             Param::text(std::to_string(incident->version())), Param::text(json(*incident).dump())});
        if (!updated) {
          co_return updated;
        }
        auto latest = co_await tx->connection().query(
            {.sql = "SELECT body::text FROM response.resources WHERE "
                    "tenant=$1::uuid AND kind='incident' AND id=$2::uuid",
             .parameters = {Param::text(origin.tenant_id()), Param::text(incident->incident_id())}});
        if (!latest) {
          co_return std::unexpected(latest.error());
        }
        const auto state = Json::parse(latest->value(0, 0).value_or("")).value("state", std::string{});
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT id::text FROM response.runs WHERE "
                    "tenant=$1::uuid AND incident=$2::uuid",
             .parameters = {Param::text(origin.tenant_id()), Param::text(incident->incident_id())}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        if (rows->size() > 0) {
          auto value = co_await load(tx, origin.tenant_id(), std::string(rows->value(0, 0).value_or("")));
          if (!value) {
            co_return std::unexpected(value.error());
          }
          if (state == "INCIDENT_STATE_CLOSED" && value->run.state() == response::v1::RUN_STATE_RUNNING) {
            co_return co_await finish(tx, &*value, d::Operation::kCancel);
          }
          co_return p::Result<void>{};
        }
        if (!input.has_incident_created() || state == "INCIDENT_STATE_CLOSED") {
          co_return p::Result<void>{};
        }
        auto plans =
            co_await tx->connection().query({.sql = "SELECT DISTINCT ON(id) body::text FROM response.plans WHERE "
                                                    "tenant=$1::uuid AND state=2 ORDER BY id,version DESC LIMIT 10000",
                                             .parameters = {Param::text(origin.tenant_id())}});
        if (!plans) {
          co_return std::unexpected(plans.error());
        }
        std::set<std::string> seen;
        std::optional<response::v1::ResponsePlan> selected;
        std::pair<int, int> score{-1, std::numeric_limits<int>::min()};
        for (std::size_t index = 0; index < plans->size(); ++index) {
          auto plan = parse<response::v1::ResponsePlan>(Json::parse(plans->value(index, 0).value_or("")));
          if (!plan) {
            co_return std::unexpected(plan.error());
          }
          if (!seen.insert(plan->plan_id()).second) {
            continue;
          }
          for (const auto& binding : plan->bindings()) {
            const std::pair<int, int> candidate{specificity(binding), binding.binding_priority()};
            if (matches(binding, incident->incident_type_id(), incident->site_id(), incident->zone_id(),
                        incident->priority())
                && candidate > score) {
              selected = *plan;
              score = candidate;
            }
          }
        }
        if (!selected) {
          response::v1::ResponseEventRecord missing;
          envelope(missing.mutable_envelope(), origin.tenant_id(), "response-engine");
          missing.mutable_plan_unavailable()->set_incident_id(incident->incident_id());
          auto emitted = co_await emit(tx, std::move(missing));
          if (!emitted) {
            co_return emitted;
          }
          ++missing_plans_;
          co_return co_await audit(tx, origin.tenant_id(), "response-engine", "response.plan.missing",
                                   incident->incident_id(), incident->site_id());
        }
        Value value{origin.tenant_id(), incident->site_id(), incident->zone_id(), *selected, {}};
        value.run.set_run_id(p::stream::uuid5(incident->incident_id(), {selected->plan_id(), "1"}));
        value.run.set_incident_id(incident->incident_id());
        value.run.set_plan_id(selected->plan_id());
        value.run.set_plan_version(selected->version());
        auto domain = model(*selected);
        if (!domain) {
          co_return std::unexpected(domain.error());
        }
        auto stored = co_await save(tx, &value, d::start(*domain, now()), "response-engine");
        if (!stored) {
          co_return stored;
        }
        co_return co_await drive(tx, &value);
      });
}

asio::awaitable<p::Result<void>> Repository::response(response::v1::ResponseEventRecord input) {
  if (!input.has_command_updated() || input.command_updated().command().run_id().empty()) {
    co_return p::Result<void>{};
  }
  const auto& command = input.command_updated().command();
  const auto tenant = input.envelope().tenant_id();
  if (!p::stream::uuid(tenant) || !p::stream::uuid(command.command_id()) || !p::stream::uuid(command.run_id())
      || command.version() == 0 || command.state() < 1 || command.state() > 10) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid command snapshot");
  }
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
      [&, this](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto value = co_await load(tx, tenant, command.run_id());
        if (!value && value.error().code() == Code::kCommonNotFound) {
          co_return p::Result<void>{};
        }
        if (!value) {
          co_return std::unexpected(value.error());
        }
        if (value->run.state() != response::v1::RUN_STATE_RUNNING || value->run.active_step_ids().empty()
            || value->run.incident_id() != command.incident_id()
            || value->run.active_step_ids(0) != command.step_id()) {
          co_return p::Result<void>{};
        }
        const auto* current = execution(&*value);
        if (current->state() != response::v1::STEP_STATE_ACTIVE
            || std::ranges::find(current->result().command_ids(), command.command_id())
                   == current->result().command_ids().end()) {
          co_return p::Result<void>{};
        }
        auto stored = co_await execute(
            tx,
            "INSERT INTO response.command_results VALUES($1::uuid,$2::uuid,$3::bigint,$4::integer) ON "
            "CONFLICT(tenant,id) "
            "DO UPDATE SET version=EXCLUDED.version,state=EXCLUDED.state WHERE "
            "response.command_results.version<EXCLUDED.version",
            {Param::text(tenant), Param::text(command.command_id()), Param::text(std::to_string(command.version())),
             Param::text(std::to_string(command.state()))});
        if (!stored) {
          co_return stored;
        }
        bool complete = true;
        bool failure = false;
        for (const auto& id : current->result().command_ids()) {
          auto rows = co_await tx->connection().query({.sql = "SELECT state FROM response.command_results WHERE "
                                                              "tenant=$1::uuid AND id=$2::uuid",
                                                       .parameters = {Param::text(tenant), Param::text(id)}});
          if (!rows) {
            co_return std::unexpected(rows.error());
          }
          const auto state = rows->size() ? std::stoi(std::string(rows->value(0, 0).value_or(""))) : 0;
          complete = complete && state == response::v1::COMMAND_STATE_EXECUTED;
          failure = failure || state >= response::v1::COMMAND_STATE_FAILED;
        }
        if (!complete && !failure) {
          co_return p::Result<void>{};
        }
        auto finished = co_await finish(tx, &*value, failure ? d::Operation::kFailure : d::Operation::kSuccess,
                                        failure ? "command did not execute successfully" : "");
        if (!finished) {
          co_return finished;
        }
        co_return co_await drive(tx, &*value);
      });
}

asio::awaitable<p::Result<Json>> Repository::request(p::security::Principal principal, std::string operation, Json body,
                                                     std::string key, std::optional<std::uint64_t> expected) {
  if (!p::stream::uuid(principal.tenant) || (!key.empty() && !p::stream::uuid(key))) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid tenant or request identity");
  }
  const bool read = operation == "list" || operation == "get" || operation == "run" || operation == "validate";
  if (!read && key.empty()) {
    co_return p::fail(Code::kCommonInvalidArgument, "Idempotency-Key required");
  }
  const auto fingerprint = p::stream::sha256(operation + body.dump());
  auto result = co_await pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
      [&, this](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        const bool plan_action = operation == "list" || operation == "get" || operation == "draft"
                                 || operation == "validate" || operation == "publish" || operation == "archive";
        std::string action;
        if (plan_action) {
          action = read ? "response.plan.read" : "response.plan.write";
        } else {
          action = read ? "response.run.read" : "response.step.execute";
        }
        if (operation == "override") {
          action = "response.step.override";
        }
        auto permission = p::authz::decide_role(principal, action, std::chrono::system_clock::now());
        if (!permission) {
          co_return std::unexpected(permission.error());
        }
        if (!read) {
          auto lock = co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,1))",
                                       {Param::text(principal.tenant + ":" + principal.subject + ":" + key)});
          if (!lock) {
            co_return std::unexpected(lock.error());
          }
          auto replayed = co_await tx->connection().query(
              {.sql = "SELECT fingerprint,result::text FROM response.requests "
                      "WHERE tenant=$1::uuid AND actor=$2 AND key=$3::uuid",
               .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key)}});
          if (!replayed) {
            co_return std::unexpected(replayed.error());
          }
          if (replayed->size()) {
            if (replayed->value(0, 0).value_or("") != fingerprint) {
              co_return p::fail(Code::kCommonIdempotencyKeyReused, "request key already used");
            }
            // Scope is checked again on every repeated read of a previous result.
            const auto output = Json::parse(replayed->value(0, 1).value_or(""));
            if (output.contains("run")) {
              auto value = co_await load(tx, principal.tenant, output.at("run").at("runId").get<std::string>());
              if (!value) {
                co_return std::unexpected(value.error());
              }
              auto scope = allowed(principal, action, *value);
              if (!scope) {
                co_return std::unexpected(scope.error());
              }
            } else if (!p::authz::all_scopes(principal)) {
              co_return p::fail(Code::kAuthScopeDenied, "global plan scope required");
            }
            co_return output;
          }
        }
        Json output = Json::object();
        if (plan_action) {
          // Plans may bind to every zone of a tenant; scoped administrators cannot publish global configuration.
          if (!p::authz::all_scopes(principal)) {
            co_return p::fail(Code::kAuthScopeDenied, "global plan scope required");
          }
          auto lock = co_await execute(tx, "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                       {Param::text(principal.tenant + ":plans")});
          if (!lock) {
            co_return std::unexpected(lock.error());
          }
          const auto id = body.value("planId", std::string{});
          if (operation == "draft" || operation == "validate") {
            auto plan = parse<response::v1::ResponsePlan>(body);
            if (!plan) {
              co_return std::unexpected(plan.error());
            }
            auto valid = co_await validate_catalog(tx, principal.tenant, *plan);
            if (operation == "validate") {
              if (!valid) {
                co_return Json{
                    {"valid", false},
                    {"violations",
                     Json::array({{{"field", "plan"}, {"description", std::string(valid.error().name())}}})}};
              }
              co_return Json{{"valid", true}, {"violations", Json::array()}};
            }
            if (!valid) {
              co_return std::unexpected(valid.error());
            }
            auto rows = co_await tx->connection().query(
                {.sql = "SELECT COALESCE(max(version),0)+1 FROM response.plans "
                        "WHERE tenant=$1::uuid AND id=$2::uuid",
                 .parameters = {Param::text(principal.tenant), Param::text(plan->plan_id())}});
            if (!rows) {
              co_return std::unexpected(rows.error());
            }
            const auto next_version = std::stoull(std::string(rows->value(0, 0).value_or("")));
            if (expected && *expected != next_version - 1) {
              co_return p::fail(Code::kCommonConcurrentModification, "plan version changed");
            }
            plan->set_version(static_cast<std::uint32_t>(next_version));
            plan->set_state(response::v1::PLAN_STATE_DRAFT);
            plan->clear_published_at();
            plan->clear_published_by();
            auto stored =
                co_await execute(tx, "INSERT INTO response.plans VALUES($1::uuid,$2::uuid,$3::integer,1,$4::jsonb)",
                                 {Param::text(principal.tenant), Param::text(plan->plan_id()),
                                  Param::text(std::to_string(plan->version())), Param::text(json(*plan).dump())});
            if (!stored) {
              co_return std::unexpected(stored.error());
            }
            output["plan"] = json(*plan);
          } else if (operation == "list") {
            const auto requested_size = body.value("pageSize", 0);
            const auto size = requested_size == 0 ? 100 : requested_size;
            const auto cursor = body.value("pageToken", std::string{});
            const auto type = body.value("incidentTypeId", std::string{});
            if (size < 1 || size > 1000 || (!cursor.empty() && !p::stream::uuid(cursor))
                || (!type.empty() && !p::stream::uuid(type))) {
              co_return p::fail(Code::kCommonInvalidArgument, "invalid plan filters");
            }
            auto rows = co_await tx->connection().query(
                {.sql =
                     "SELECT body::text FROM (SELECT DISTINCT ON(id) id,state,body FROM response.plans WHERE "
                     "tenant=$1::uuid ORDER BY id,version DESC) latest WHERE ($2='' OR id>NULLIF($2,'')::uuid) "
                     "AND ($3::boolean OR state<>3) AND ($4='' OR EXISTS(SELECT 1 FROM "
                     "jsonb_array_elements(COALESCE(body->'bindings','[]'::jsonb)) b WHERE b->>'incidentTypeId'=$4)) "
                     "ORDER BY id LIMIT $5::integer",
                 .parameters = {Param::text(principal.tenant), Param::text(cursor),
                                Param::text(body.value("includeArchived", false) ? "true" : "false"), Param::text(type),
                                Param::text(std::to_string(size + 1))}});
            if (!rows) {
              co_return std::unexpected(rows.error());
            }
            output["plans"] = Json::array();
            for (std::size_t index = 0; index < rows->size(); ++index) {
              if (std::cmp_equal(index, size)) {
                output["nextPageToken"] = output["plans"].back()["planId"];
                break;
              }
              output["plans"].push_back(Json::parse(rows->value(index, 0).value_or("")));
            }
          } else {
            if (!p::stream::uuid(id)) {
              co_return p::fail(Code::kCommonInvalidArgument, "invalid plan identity");
            }
            const auto version = body.value("version", 0U);
            auto rows = co_await tx->connection().query(
                {.sql = "SELECT body::text,state FROM response.plans WHERE tenant=$1::uuid AND id=$2::uuid AND "
                        "($3::integer=0 OR version=$3::integer) ORDER BY version DESC LIMIT 1",
                 .parameters = {Param::text(principal.tenant), Param::text(id), Param::text(std::to_string(version))}});
            if (!rows) {
              co_return std::unexpected(rows.error());
            }
            if (!rows->size()) {
              co_return p::fail(Code::kCommonNotFound, "plan unavailable");
            }
            auto plan = parse<response::v1::ResponsePlan>(Json::parse(rows->value(0, 0).value_or("")));
            if (!plan) {
              co_return std::unexpected(plan.error());
            }
            if (operation == "publish") {
              if (version == 0 || rows->value(0, 1).value_or("") != "1") {
                co_return p::fail(Code::kResponseStepNotActive, "draft version required");
              }
              auto valid = co_await validate_catalog(tx, principal.tenant, *plan);
              if (!valid) {
                co_return std::unexpected(valid.error());
              }
              auto other = co_await tx->connection().query(
                  {.sql = "SELECT DISTINCT ON(id) body::text FROM response.plans "
                          "WHERE tenant=$1::uuid AND id<>$2::uuid AND state=2 ORDER BY id,version DESC",
                   .parameters = {Param::text(principal.tenant), Param::text(id)}});
              if (!other) {
                co_return std::unexpected(other.error());
              }
              for (std::size_t index = 0; index < other->size(); ++index) {
                const auto candidate =
                    parse<response::v1::ResponsePlan>(Json::parse(other->value(index, 0).value_or("")));
                if (!candidate) {
                  co_return std::unexpected(candidate.error());
                }
                for (const auto& left : plan->bindings()) {
                  for (const auto& right : candidate->bindings()) {
                    if (overlaps(left, right)) {
                      co_return p::fail(Code::kResponsePlanInvalid, "ambiguous plan binding");
                    }
                  }
                }
              }
              plan->set_state(response::v1::PLAN_STATE_PUBLISHED);
              *plan->mutable_published_at() = Time::GetCurrentTime();
              plan->mutable_published_by()->set_id(principal.subject);
              plan->mutable_published_by()->set_kind(common::v1::ACTOR_KIND_USER);
            } else if (operation == "archive") {
              plan->set_state(response::v1::PLAN_STATE_ARCHIVED);
            }
            if (operation != "get") {
              auto stored = co_await execute(
                  tx,
                  "UPDATE response.plans SET state=$3::integer,body=CASE WHEN $3::integer=3 THEN "
                  "jsonb_set(body,'{state}','\"PLAN_STATE_ARCHIVED\"') ELSE $4::jsonb END WHERE tenant=$1::uuid AND "
                  "id=$2::uuid "
                  "AND (version=$5::integer OR $3::integer=3)",
                  {Param::text(principal.tenant), Param::text(id), Param::text(std::to_string(plan->state())),
                   Param::text(json(*plan).dump()), Param::text(std::to_string(plan->version()))});
              if (!stored) {
                co_return std::unexpected(stored.error());
              }
            }
            output["plan"] = json(*plan);
          }
        } else {
          std::string id = body.value("runId", std::string{});
          if (operation == "run") {
            const auto incident_id = body.value("incidentId", std::string{});
            if (!p::stream::uuid(incident_id)) {
              co_return p::fail(Code::kCommonInvalidArgument, "invalid incident identity");
            }
            auto rows = co_await tx->connection().query(
                {.sql = "SELECT id::text FROM response.runs WHERE "
                        "tenant=$1::uuid AND incident=$2::uuid",
                 .parameters = {Param::text(principal.tenant), Param::text(incident_id)}});
            if (!rows) {
              co_return std::unexpected(rows.error());
            }
            if (!rows->size()) {
              co_return p::fail(Code::kCommonNotFound, "run unavailable");
            }
            id = std::string(rows->value(0, 0).value_or(""));
          }
          if (!p::stream::uuid(id)) {
            co_return p::fail(Code::kCommonInvalidArgument, "invalid run identity");
          }
          auto value = co_await load(tx, principal.tenant, id);
          if (!value) {
            co_return std::unexpected(value.error());
          }
          auto scope = allowed(principal, action, *value);
          if (!scope) {
            co_return std::unexpected(scope.error());
          }
          if (operation == "run") {
            co_return Json{{"run", json(value->run)}, {"plan", json(value->plan)}};
          }
          if (!expected) {
            co_return p::fail(Code::kCommonPreconditionRequired, "If-Match required");
          }
          if (*expected != value->run.version()) {
            co_return p::fail(Code::kCommonConcurrentModification, "run version differs");
          }
          if (value->run.state() != response::v1::RUN_STATE_RUNNING || value->run.active_step_ids().empty()
              || body.value("stepId", std::string{}) != value->run.active_step_ids(0)) {
            co_return p::fail(Code::kResponseStepNotActive, "step is not active");
          }
          const auto& step = active(*value);
          d::Operation change = d::Operation::kComplete;
          d::Result details;
          if (operation == "complete") {
            auto input = parse<response::v1::StepResult>(body.value("result", Json::object()));
            if (!input || input->overridden() || !input->command_ids().empty()) {
              co_return p::fail(Code::kCommonInvalidArgument, "client cannot set command results or override flag");
            }
            details.choice = input->decision_option_id();
            details.note = input->note();
            details.checked.assign(input->checked_item_ids().begin(), input->checked_item_ids().end());
          } else {
            details.note = body.value("reason", std::string{});
            if (operation == "skip") {
              change = d::Operation::kSkip;
            } else if (operation == "retry") {
              change = d::Operation::kRetry;
            } else if (operation == "override") {
              change = d::Operation::kOverride;
            } else {
              co_return p::fail(Code::kCommonNotFound, "unknown step operation");
            }
          }
          if (details.note.size() > 4096) {
            co_return p::fail(Code::kCommonInvalidArgument, "reason exceeds limit");
          }
          if (step.has_command() && operation == "complete") {
            if (execution(&*value)->state() != response::v1::STEP_STATE_ACTIVE
                || !execution(&*value)->result().command_ids().empty()) {
              co_return p::fail(Code::kResponseStepNotActive, "command already requested or failed");
            }
            auto driven = co_await drive(tx, &*value, principal);
            if (!driven) {
              co_return std::unexpected(driven.error());
            }
          } else {
            auto changes = advance(*value, change, details, principal.roles.contains("supervisor"));
            if (!changes) {
              co_return std::unexpected(changes.error());
            }
            auto stored = co_await save(tx, &*value, std::move(*changes), principal.subject);
            if (!stored) {
              co_return std::unexpected(stored.error());
            }
            auto driven = co_await drive(tx, &*value);
            if (!driven) {
              co_return std::unexpected(driven.error());
            }
          }
          output["run"] = json(value->run);
        }
        if (!read) {
          auto stored = co_await execute(tx, "INSERT INTO response.requests VALUES($1::uuid,$2,$3::uuid,$4,$5::jsonb)",
                                         {Param::text(principal.tenant), Param::text(principal.subject),
                                          Param::text(key), Param::text(fingerprint), Param::text(output.dump())});
          if (!stored) {
            co_return std::unexpected(stored.error());
          }
          const auto id = output.contains("run") ? output["run"].at("runId").get<std::string>()
                                                 : output["plan"].at("planId").get<std::string>();
          auto recorded = co_await audit(tx, principal.tenant, principal.subject, "response." + operation, id, {});
          if (!recorded) {
            co_return std::unexpected(recorded.error());
          }
        }
        co_return output;
      });
  if (!result && (result.error().code() == Code::kAuthForbidden || result.error().code() == Code::kAuthScopeDenied)) {
    auto recorded = co_await pg::in_transaction<void>(
        pool_,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
        [&, this](pg::Transaction* tx) {
          return audit(tx, principal.tenant, principal.subject, "response." + operation,
                       body.value("runId", body.value("planId", std::string{})), {}, true);
        });
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
  }
  co_return result;
}

asio::awaitable<p::Result<void>> Repository::tick() {
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
  for (const auto& timer : *due) {
    auto fired = co_await pg::in_transaction<void>(
        pool_,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
        [&, this](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          auto value = co_await load(tx, timer.tenant, timer.owner);
          if (!value) {
            co_return std::unexpected(value.error());
          }
          auto claimed = co_await timers_.fire(tx, timer);
          if (!claimed) {
            co_return std::unexpected(claimed.error());
          }
          if (!*claimed || value->run.state() != response::v1::RUN_STATE_RUNNING
              || value->run.active_step_ids().empty()) {
            co_return p::Result<void>{};
          }
          const auto& step = active(*value);
          const auto* current = execution(&*value);
          if (timer.id != effect_id(value->run, step.step_id(), current->attempt())
              || current->state() != response::v1::STEP_STATE_ACTIVE) {
            co_return p::Result<void>{};
          }
          auto finished =
              co_await finish(tx, &*value, step.has_wait() ? d::Operation::kSuccess : d::Operation::kFailure,
                              step.has_wait() ? "" : "handler outcome unknown after deadline");
          if (!finished) {
            co_return finished;
          }
          co_return co_await drive(tx, &*value);
        });
    if (!fired) {
      co_return fired;
    }
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::effects() {
  if (!handler_) {
    co_return p::Result<void>{};
  }
  p::Result<pg::Rows> rows = p::fail(Code::kCommonInternal, "pending handler query unavailable");
  {
    auto lease = co_await pool_->acquire();
    if (!lease) {
      co_return std::unexpected(lease.error());
    }
    rows = co_await (*lease)->query({.sql = "SELECT tenant::text,run::text,id::text FROM response.effects WHERE "
                                            "status='pending' ORDER BY id LIMIT 16"});
  }
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  for (std::size_t index = 0; index < rows->size(); ++index) {
    const auto tenant = std::string(rows->value(index, 0).value_or(""));
    const auto run = std::string(rows->value(index, 1).value_or(""));
    const auto id = std::string(rows->value(index, 2).value_or(""));
    auto claimed = co_await pg::in_transaction<Json>(
        pool_,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
        [&, this](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
          auto value = co_await load(tx, tenant, run);
          if (!value) {
            co_return std::unexpected(value.error());
          }
          if (value->run.state() != response::v1::RUN_STATE_RUNNING || value->run.active_step_ids().empty()) {
            co_return Json();
          }
          auto* current = execution(&*value);
          if (current->state() != response::v1::STEP_STATE_ACTIVE
              || id != effect_id(value->run, current->step_id(), current->attempt())) {
            co_return Json();
          }
          auto pending = co_await tx->connection().query(
              {.sql = "UPDATE response.effects SET status='dispatched' WHERE "
                      "tenant=$1::uuid AND id=$2::uuid AND status='pending' RETURNING url,timeout",
               .parameters = {Param::text(tenant), Param::text(id)}});
          if (!pending) {
            co_return std::unexpected(pending.error());
          }
          if (!pending->size()) {
            co_return Json();
          }
          const auto timeout = std::stoi(std::string(pending->value(0, 1).value_or("")));
          auto timer = co_await timers_.schedule(tx, {tenant, run, id, now() + (std::int64_t{timeout} * 1000)});
          if (!timer) {
            co_return std::unexpected(timer.error());
          }
          co_return Json{{"url", std::string(pending->value(0, 0).value_or(""))},
                         {"timeout", timeout},
                         {"effectId", id},
                         {"runId", run},
                         {"incidentId", value->run.incident_id()},
                         {"stepId", current->step_id()},
                         {"tenantId", tenant}};
        });
    if (!claimed) {
      co_return std::unexpected(claimed.error());
    }
    if (claimed->is_null()) {
      continue;
    }
    const auto url = claimed->at("url").get<std::string>();
    auto outcome = handlers_.contains(url) ? co_await handler_(url, *claimed, claimed->at("timeout").get<int>())
                                           : p::Result<bool>(false);
    auto completed = co_await pg::in_transaction<void>(
        pool_,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
        [&, this](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          auto value = co_await load(tx, tenant, run);
          if (!value) {
            co_return std::unexpected(value.error());
          }
          auto stored = co_await execute(
              tx, "UPDATE response.effects SET status='finished' WHERE tenant=$1::uuid AND id=$2::uuid",
              {Param::text(tenant), Param::text(id)});
          if (!stored) {
            co_return stored;
          }
          if (value->run.state() != response::v1::RUN_STATE_RUNNING || value->run.active_step_ids().empty()) {
            co_return p::Result<void>{};
          }
          const auto* current = execution(&*value);
          if (current->state() != response::v1::STEP_STATE_ACTIVE
              || id != effect_id(value->run, current->step_id(), current->attempt())) {
            co_return p::Result<void>{};
          }
          auto finished =
              co_await finish(tx, &*value, outcome && *outcome ? d::Operation::kSuccess : d::Operation::kFailure,
                              outcome && *outcome ? "" : "handler failed or outcome unknown");
          if (!finished) {
            co_return finished;
          }
          co_return co_await drive(tx, &*value);
        });
    if (!completed) {
      co_return completed;
    }
  }
  co_return p::Result<void>{};
}
}  // namespace psim::response_engine
