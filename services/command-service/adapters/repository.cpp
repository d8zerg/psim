#include "repository.hpp"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/api/v1/commands.pb.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/response/v1/events.pb.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "limits.hpp"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/common/v1/envelope.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/domain/response/command.hpp"
#include "psim/incident/v1/events.pb.h"
#include "psim/incident/v1/incident.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/response/v1/command.pb.h"

namespace psim::command_service {
namespace pg = p::pg;
namespace d = domain::response;
using Param = pg::Parameter;
using Code = p::ErrorCode;
using Time = google::protobuf::util::TimeUtil;

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

std::string preview_fingerprint(Json request, const catalog::v1::Device& device, const catalog::v1::DeviceType& type) {
  request.erase("previewFingerprint");
  return p::stream::sha256(request.dump() + json(device).dump() + json(type).dump());
}

p::security::Principal system(const std::string& tenant) {
  return {.subject = "command-service", .tenant = tenant, .expires_at = std::chrono::system_clock::time_point::max()};
}

void envelope(common::v1::Envelope* out, const p::security::Principal& actor, std::uint64_t version = 1) {
  out->set_message_id(p::stream::uuid7());
  out->set_tenant_id(actor.tenant);
  out->set_sequence(version);
  out->set_correlation_id(out->message_id());
  out->set_schema_version("1");
  *out->mutable_occurred_at() = Time::GetCurrentTime();
  *out->mutable_produced_at() = out->occurred_at();
  out->mutable_actor()->set_id(actor.subject);
  out->mutable_actor()->set_kind((actor.subject == "command-service" || actor.subject == "response-engine")
                                     ? common::v1::ACTOR_KIND_SERVICE
                                     : common::v1::ACTOR_KIND_USER);
}

p::Result<void> access(const p::security::Principal& actor, std::string_view action, const Value& value) {
  auto allowed =
      p::authz::decide(actor, action, {value.tenant, value.site, value.zone}, std::chrono::system_clock::now());
  if (allowed && !value.command.incident_id().empty()) {
    allowed = p::authz::decide(actor, action, {value.tenant, value.incident_site, value.incident_zone},
                               std::chrono::system_clock::now());
  }
  return allowed;
}

asio::awaitable<p::Result<Json>> resource(pg::Transaction* tx, std::string tenant, std::string kind, std::string id) {
  auto rows = co_await tx->connection().query(
      {.sql = "SELECT body::text FROM command.resources WHERE tenant=$1::uuid AND kind=$2 AND id=$3::uuid FOR SHARE",
       .parameters = {Param::text(tenant), Param::text(kind), Param::text(id)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0) {
    co_return p::fail(Code::kCommonNotFound, "resource unavailable");
  }
  co_return Json::parse(rows->value(0, 0).value_or(""));
}

asio::awaitable<p::Result<Value>> load(pg::Transaction* tx, std::string tenant, std::string id) {
  auto rows = co_await tx->connection().query(
      {.sql = "SELECT body::text,site::text,zone::text,incident_site,incident_zone,session,external,fingerprint FROM "
              "command.commands WHERE tenant=$1::uuid AND id=$2::uuid FOR UPDATE",
       .parameters = {Param::text(tenant), Param::text(id)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0) {
    co_return p::fail(Code::kCommonNotFound, "command unavailable");
  }
  auto parsed = parse<response::v1::Command>(Json::parse(rows->value(0, 0).value_or("")));
  if (!parsed) {
    co_return std::unexpected(parsed.error());
  }
  co_return Value{std::move(*parsed),
                  std::move(tenant),
                  std::string(rows->value(0, 1).value_or("")),
                  std::string(rows->value(0, 2).value_or("")),
                  std::string(rows->value(0, 3).value_or("")),
                  std::string(rows->value(0, 4).value_or("")),
                  std::string(rows->value(0, 5).value_or("")),
                  std::string(rows->value(0, 6).value_or("")),
                  std::string(rows->value(0, 7).value_or(""))};
}

p::Result<bool> capability(const catalog::v1::DeviceType& type, const response::v1::Command& command) {
  const auto found = std::ranges::find(type.capabilities(), command.capability(), &catalog::v1::Capability::code);
  if (found == type.capabilities().end() || type.state() != catalog::v1::RESOURCE_STATE_ACTIVE) {
    return p::fail(Code::kCommandCapabilityUnsupported, "device capability unavailable");
  }
  std::set<std::string> names;
  for (const auto& spec : found->parameters()) {
    names.insert(spec.name());
    const auto input = command.parameters().find(spec.name());
    if (input == command.parameters().end()) {
      if (spec.required()) {
        return p::fail(Code::kCommonInvalidArgument, "required parameter missing");
      }
      continue;
    }
    const auto& text = input->second;
    if (text.size() > 1024
        || (!spec.allowed_values().empty()
            && std::ranges::find(spec.allowed_values(), text) == spec.allowed_values().end())) {
      return p::fail(Code::kCommonInvalidArgument, "parameter outside allowed values");
    }
    bool valid = true;
    if (spec.type() == catalog::v1::PARAMETER_TYPE_INTEGER) {
      std::int64_t number = 0;
      const auto result = std::from_chars(std::string_view(text).begin(), std::string_view(text).end(), number);
      valid = result.ec == std::errc{} && result.ptr == std::string_view(text).end();
    } else if (spec.type() == catalog::v1::PARAMETER_TYPE_NUMBER) {
      double number = 0;
      const auto result = std::from_chars(std::string_view(text).begin(), std::string_view(text).end(), number);
      valid = result.ec == std::errc{} && result.ptr == std::string_view(text).end() && std::isfinite(number);
    } else if (spec.type() == catalog::v1::PARAMETER_TYPE_BOOLEAN) {
      valid = text == "true" || text == "false";
    } else if (spec.type() == catalog::v1::PARAMETER_TYPE_DURATION) {
      google::protobuf::Duration duration;
      valid = Time::FromString(text, &duration) && duration.seconds() >= 0 && duration.nanos() >= 0;
    } else {
      valid = spec.type() == catalog::v1::PARAMETER_TYPE_STRING;
    }
    if (!valid) {
      return p::fail(Code::kCommonInvalidArgument, "parameter type mismatch");
    }
  }
  for (const auto& [name, text] : command.parameters()) {
    (void)text;
    if (!names.contains(name)) {
      return p::fail(Code::kCommonInvalidArgument, "unknown parameter");
    }
  }
  return found->critical();
}

p::Result<void> transition(Value& value, d::Action action, std::string_view actor = {}) {
  auto& command = value.command;
  const auto next = d::apply({static_cast<d::State>(command.state()), command.requested_by().id(),
                              Time::TimestampToNanoseconds(command.deadline_at())},
                             action, actor, Time::TimestampToNanoseconds(Time::GetCurrentTime()));
  if (!next) {
    return p::fail(next.error() == d::Failure::kSelfApproval ? Code::kCommandApprovalSelf : Code::kCommandInvalidState,
                   "invalid command transition");
  }
  const auto previous = command.state();
  command.set_state(static_cast<response::v1::CommandState>(*next));
  if (*next == d::State::kTimedOut) {
    command.set_result_code(previous == response::v1::COMMAND_STATE_AWAITING_APPROVAL ? "COMMAND_APPROVAL_TIMED_OUT"
                                                                                      : "COMMAND_TIMED_OUT");
  } else if (*next == d::State::kUnknown) {
    command.set_result_code("COMMAND_OUTCOME_UNKNOWN");
  }
  return {};
}
}  // namespace

asio::awaitable<p::Result<void>> Repository::requested(response::v1::ResponseEventRecord input) {
  if (!input.has_command_requested()) {
    co_return p::Result<void>{};
  }
  const auto& origin = input.envelope();
  const auto& intent = input.command_requested();
  const auto& command = intent.command();
  if (origin.actor().kind() != common::v1::ACTOR_KIND_SERVICE || origin.actor().id() != "response-engine"
      || !p::stream::uuid(origin.tenant_id()) || !p::stream::uuid(origin.message_id())
      || !p::stream::uuid(command.command_id()) || !p::stream::uuid(command.run_id())
      || !p::stream::uuid(command.device_id()) || !p::stream::uuid(command.incident_id())
      || !p::stream::uuid(intent.site_id()) || !p::stream::uuid(intent.zone_id()) || command.step_id().empty()
      || (command.requested_by().kind() != common::v1::ACTOR_KIND_USER
          && command.requested_by().kind() != common::v1::ACTOR_KIND_SERVICE)
      || (command.requested_by().kind() == common::v1::ACTOR_KIND_USER && !p::stream::uuid(command.requested_by().id()))
      || (command.requested_by().kind() == common::v1::ACTOR_KIND_SERVICE
          && command.requested_by().id() != "response-engine")) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid response command intent");
  }
  p::security::Principal actor{.subject = command.requested_by().id(),
                               .tenant = origin.tenant_id(),
                               .roles = {"operator"},
                               .scopes = {"zone:" + intent.zone_id()},
                               .expires_at = std::chrono::system_clock::time_point::max()};
  api::v1::RequestCommandRequest body;
  body.set_command_id(command.command_id());
  body.set_device_id(command.device_id());
  body.set_capability(command.capability());
  *body.mutable_parameters() = command.parameters();
  body.set_incident_id(command.incident_id());
  body.set_run_id(command.run_id());
  body.set_step_id(command.step_id());
  auto result = co_await request(actor, "request", json(body), command.command_id());
  if (result) {
    co_return p::Result<void>{};
  }
  if (result.error().retryable()) {
    co_return std::unexpected(result.error());
  }
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
      [&, this](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto lock =
            co_await tx->connection().query({.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                             .parameters = {Param::text(actor.tenant + ":" + command.command_id())}});
        if (!lock) {
          co_return std::unexpected(lock.error());
        }
        auto previous = co_await load(tx, actor.tenant, command.command_id());
        if (previous) {
          co_return p::Result<void>{};
        }
        if (previous.error().code() != Code::kCommonNotFound) {
          co_return std::unexpected(previous.error());
        }
        auto accepted = co_await messages_.accept(tx, "response", origin.message_id(),
                                                  p::stream::sha256(input.SerializeAsString()));
        if (!accepted) {
          co_return std::unexpected(accepted.error());
        }
        if (!*accepted) {
          co_return p::Result<void>{};
        }
        Value value;
        value.tenant = actor.tenant;
        value.site = intent.site_id();
        value.zone = intent.zone_id();
        value.incident_site = value.site;
        value.incident_zone = value.zone;
        value.command = command;
        value.command.set_state(response::v1::COMMAND_STATE_FAILED);
        value.command.set_result_code(std::string(result.error().name()));
        *value.command.mutable_requested_by() = command.requested_by();
        *value.command.mutable_requested_at() = Time::GetCurrentTime();
        *value.command.mutable_deadline_at() = value.command.requested_at();
        co_return co_await save(tx, std::move(value), actor, "command.request.failed");
      });
}

asio::awaitable<p::Result<void>> Repository::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto initialized = co_await messages_.initialize(&**lease);
  if (!initialized) {
    co_return initialized;
  }
  co_return co_await pg::Migrator("command").apply(
      &**lease,
      {{.version = 1,
        .name = "durable device commands",
        .expand = {"CREATE TABLE command.resources(tenant uuid NOT NULL,kind text NOT NULL,id uuid NOT NULL,version "
                   "bigint NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,kind,id))",
                   "CREATE TABLE command.commands(tenant uuid NOT NULL,id uuid NOT NULL,site uuid NOT NULL,zone uuid "
                   "NOT NULL,incident_site text NOT NULL,incident_zone text NOT NULL,session text NOT NULL,external "
                   "text NOT NULL,fingerprint text NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,id))",
                   "CREATE TABLE command.requests(tenant uuid NOT NULL,actor text NOT NULL,key uuid NOT "
                   "NULL,fingerprint text NOT NULL,PRIMARY KEY(tenant,actor,key))",
                   "CREATE INDEX command_pending ON command.commands((body->>'deadlineAt')) WHERE body->>'state' IN "
                   "('COMMAND_STATE_REQUESTED','COMMAND_STATE_AWAITING_APPROVAL','COMMAND_STATE_SENT','COMMAND_STATE_"
                   "ACKNOWLEDGED')"}}});
}

asio::awaitable<p::Result<void>> Repository::project(std::string tenant, std::string kind, std::string id,
                                                     std::uint64_t version, Json body) {
  if (!p::stream::uuid(tenant) || !p::stream::uuid(id) || version == 0) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid resource identity or version");
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows = co_await (*lease)->query(
      {.sql = "INSERT INTO command.resources VALUES($1::uuid,$2,$3::uuid,$4,$5::jsonb) ON CONFLICT(tenant,kind,id) DO "
              "UPDATE SET version=EXCLUDED.version,body=EXCLUDED.body WHERE command.resources.version<EXCLUDED.version",
       .parameters = {Param::text(tenant), Param::text(kind), Param::text(id),
                      Param::integer(static_cast<std::int64_t>(version)), Param::text(body.dump())}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::catalog(catalog::v1::CatalogRecord input) {
  if (input.has_device()) {
    co_return co_await project(input.envelope().tenant_id(), "device", input.device().device_id(),
                               input.device().version(), json(input.device()));
  }
  if (input.has_device_type()) {
    co_return co_await project(input.envelope().tenant_id(), "type", input.device_type().device_type_id(),
                               input.device_type().version(), json(input.device_type()));
  }
  if (input.has_connector()) {
    co_return co_await project(input.envelope().tenant_id(), "connector", input.connector().connector_id(),
                               input.connector().version(), json(input.connector()));
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::incident(incident::v1::IncidentEventRecord input) {
  const auto* field = incident::v1::IncidentEventRecord::GetReflection()->GetOneofFieldDescriptor(
      input, incident::v1::IncidentEventRecord::GetDescriptor()->FindOneofByName("payload"));
  if (!field) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "missing incident payload");
  }
  const auto& payload = incident::v1::IncidentEventRecord::GetReflection()->GetMessage(input, field);
  const auto* parent = payload.GetDescriptor()->FindFieldByName("incident");
  if (!parent) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "missing incident snapshot");
  }
  auto parsed = parse<incident::v1::Incident>(json(payload.GetReflection()->GetMessage(payload, parent)));
  if (!parsed) {
    co_return std::unexpected(parsed.error());
  }
  co_return co_await project(input.envelope().tenant_id(), "incident", parsed->incident_id(), parsed->version(),
                             json(*parsed));
}

asio::awaitable<p::Result<void>> Repository::audit(pg::Transaction* tx, p::security::Principal actor,
                                                   std::string action, std::string id, std::string site, bool denied) {
  audit::v1::AuditEventRecord output;
  envelope(output.mutable_envelope(), actor);
  auto* entry = output.mutable_audit_entry();
  *entry->mutable_actor() = output.envelope().actor();
  entry->set_action(denied ? "access.denied" : action);
  if (denied) {
    (*entry->mutable_details())["requested_action"] = std::move(action);
  }
  entry->set_object_type("command");
  entry->set_object_id(std::move(id));
  entry->set_site_id(std::move(site));
  entry->set_outcome(denied ? audit::v1::AUDIT_OUTCOME_DENIED : audit::v1::AUDIT_OUTCOME_SUCCESS);
  co_return co_await messages_.append(tx,
                                      {output.envelope().message_id(), encode_("psim.audit.v1", actor.tenant, output)});
}

asio::awaitable<p::Result<void>> Repository::save(pg::Transaction* tx, Value value, p::security::Principal actor,
                                                  std::string action) {
  auto command = value.command;
  command.set_version(command.version() + 1);
  if (d::terminal(static_cast<d::State>(command.state()))) {
    *command.mutable_finished_at() = Time::GetCurrentTime();
  }
  auto saved = co_await tx->connection().query(
      {.sql = "INSERT INTO command.commands VALUES($1::uuid,$2::uuid,$3::uuid,$4::uuid,$5,$6,$7,$8,$9,$10::jsonb) ON "
              "CONFLICT(tenant,id) DO UPDATE SET session=EXCLUDED.session,body=EXCLUDED.body",
       .parameters = {Param::text(value.tenant), Param::text(command.command_id()), Param::text(value.site),
                      Param::text(value.zone), Param::text(value.incident_site), Param::text(value.incident_zone),
                      Param::text(value.session), Param::text(value.external), Param::text(value.fingerprint),
                      Param::text(json(command).dump())}});
  if (!saved) {
    co_return std::unexpected(saved.error());
  }
  response::v1::ResponseEventRecord event;
  envelope(event.mutable_envelope(), actor, command.version());
  *event.mutable_command_updated()->mutable_command() = command;
  auto appended = co_await messages_.append(
      tx, {event.envelope().message_id(),
           encode_("psim.response.events.v1",
                   command.incident_id().empty() ? command.command_id() : command.incident_id(), event)});
  if (!appended) {
    co_return appended;
  }
  co_return co_await audit(tx, std::move(actor), std::move(action), command.command_id(), value.site);
}

asio::awaitable<p::Result<void>> Repository::dispatch(pg::Transaction* tx, Value* value) {
  auto raw = co_await resource(tx, value->tenant, "device", value->command.device_id());
  if (!raw) {
    co_return std::unexpected(raw.error());
  }
  auto device = parse<catalog::v1::Device>(*raw);
  if (!device) {
    co_return std::unexpected(device.error());
  }
  auto type_raw = co_await resource(tx, value->tenant, "type", device->device_type_id());
  if (!type_raw) {
    co_return std::unexpected(type_raw.error());
  }
  auto type = parse<catalog::v1::DeviceType>(*type_raw);
  if (!type) {
    co_return std::unexpected(type.error());
  }
  auto connector_raw = co_await resource(tx, value->tenant, "connector", device->connector_id());
  if (!connector_raw) {
    co_return std::unexpected(connector_raw.error());
  }
  auto connector = parse<catalog::v1::Connector>(*connector_raw);
  if (!connector) {
    co_return std::unexpected(connector.error());
  }
  auto supported = capability(*type, value->command);
  if (!supported || (*supported && !value->command.critical()) || device->state() != catalog::v1::RESOURCE_STATE_ACTIVE
      || connector->state() != catalog::v1::RESOURCE_STATE_ACTIVE || device->site_id() != value->site
      || device->zone_id() != value->zone || device->connector_id() != value->command.connector_id()
      || device->external_id() != value->external) {
    co_return p::fail(Code::kCommandCapabilityUnsupported, "device configuration changed");
  }
  if (!value->command.incident_id().empty()) {
    auto parent = co_await resource(tx, value->tenant, "incident", value->command.incident_id());
    if (!parent) {
      co_return std::unexpected(parent.error());
    }
    if (parent->value("state", std::string{}) == "INCIDENT_STATE_CLOSED") {
      co_return p::fail(Code::kCommandInvalidState, "incident closed");
    }
  }
  auto session = co_await sessions_(value->tenant, value->command.connector_id());
  if (!session) {
    co_return std::unexpected(session.error());
  }
  if (!*session) {
    auto failed = transition(*value, d::Action::kFailure);
    if (!failed) {
      co_return failed;
    }
    if (value->command.state() == response::v1::COMMAND_STATE_FAILED) {
      value->command.set_result_code("COMMAND_CONNECTOR_OFFLINE");
    }
    co_return p::Result<void>{};
  }
  value->session = **session;
  auto changed = transition(*value, d::Action::kSend);
  if (!changed) {
    co_return changed;
  }
  if (value->command.state() != response::v1::COMMAND_STATE_SENT) {
    co_return p::Result<void>{};
  }
  response::v1::CommandDeliveryRecord output;
  envelope(output.mutable_envelope(), system(value->tenant));
  output.mutable_envelope()->set_correlation_id(value->command.command_id());
  auto* delivery = output.mutable_command_delivery();
  delivery->set_command_id(value->command.command_id());
  delivery->set_connector_id(value->command.connector_id());
  delivery->set_device_external_id(value->external);
  delivery->set_capability(value->command.capability());
  *delivery->mutable_parameters() = value->command.parameters();
  *delivery->mutable_deadline_at() = value->command.deadline_at();
  delivery->set_session_id(value->session);
  co_return co_await messages_.append(
      tx, {output.envelope().message_id(), encode_("psim.commands.v1", delivery->connector_id(), output)});
}

asio::awaitable<p::Result<Json>> Repository::request(p::security::Principal principal, std::string operation, Json body,
                                                     std::string key, std::optional<std::uint64_t> expected) {
  if (!p::stream::uuid(principal.tenant)) {
    co_return p::fail(Code::kAuthUnauthenticated, "tenant required");
  }
  const auto id = body.value("commandId", std::string{});
  if (operation != "list" && !p::stream::uuid(id)) {
    co_return p::fail(Code::kCommonInvalidArgument, "command identity required");
  }
  if (operation == "request" && key != id) {
    co_return p::fail(Code::kCommonInvalidArgument, "Idempotency-Key must equal command_id");
  }
  if (operation != "request" && operation != "list" && operation != "get" && !p::stream::uuid(key)) {
    co_return p::fail(Code::kCommonInvalidArgument, "Idempotency-Key required");
  }
  auto result = co_await pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned until awaited.
      [this, &principal, &operation, &body, &id, &expected,
       &key](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        std::string action = "command.confirm";
        if (operation == "get" || operation == "list") {
          action = "command.read";
        } else if (operation == "request" || operation == "preview") {
          action = "command.request";
        } else if (operation == "cancel") {
          action = "command.cancel";
        }
        auto allowed = p::authz::decide_role(principal, action, std::chrono::system_clock::now());
        if (!allowed) {
          co_return std::unexpected(allowed.error());
        }
        if (operation == "list") {
          auto query = parse<api::v1::ListCommandsRequest>(body);
          if (!query) {
            co_return std::unexpected(query.error());
          }
          if (query->page_size() < 0 || query->page_size() > 1000
              || (!query->page_token().empty() && !p::stream::uuid(query->page_token()))
              || (!query->device_id().empty() && !p::stream::uuid(query->device_id()))
              || (!query->incident_id().empty() && !p::stream::uuid(query->incident_id()))) {
            co_return p::fail(Code::kCommonInvalidArgument, "invalid list filters");
          }
          for (const auto state : query->states()) {
            if (state < 1 || state > 10) {
              co_return p::fail(Code::kCommonInvalidArgument, "invalid state");
            }
          }
          const auto limit = query->page_size() == 0 ? 100 : query->page_size();
          Json output = {{"commands", Json::array()}};
          std::string cursor = query->page_token();
          for (int batch = 0; batch < 100; ++batch) {
            auto rows = co_await tx->connection().query(
                {.sql = "SELECT id::text FROM command.commands WHERE tenant=$1::uuid AND ($2='' OR "
                        "id>NULLIF($2,'')::uuid) AND ($3='' OR body->>'deviceId'=$3) AND ($4='' OR "
                        "body->>'incidentId'=$4) ORDER BY id LIMIT 1000",
                 .parameters = {Param::text(principal.tenant), Param::text(cursor), Param::text(query->device_id()),
                                Param::text(query->incident_id())}});
            if (!rows) {
              co_return std::unexpected(rows.error());
            }
            for (std::size_t index = 0; index < rows->size(); ++index) {
              cursor = std::string(rows->value(index, 0).value_or(""));
              auto value = co_await load(tx, principal.tenant, cursor);
              if (!value) {
                co_return std::unexpected(value.error());
              }
              if (!access(principal, action, *value)) {
                continue;
              }
              if (!query->states().empty()
                  && std::ranges::find(query->states(), static_cast<int>(value->command.state()))
                         == query->states().end()) {
                continue;
              }
              if (output["commands"].size() == static_cast<std::size_t>(limit)) {
                co_return output;
              }
              output["commands"].push_back(json(value->command));
              output["nextPageToken"] = cursor;
            }
            if (rows->size() < 1000) {
              output.erase("nextPageToken");
              co_return output;
            }
          }
          // Continue scanning on the next request even when every scanned row is outside scope.
          output["nextPageToken"] = cursor;
          co_return output;
        }
        if (operation != "get") {
          auto locked = co_await tx->connection().query(
              {.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,1))",
               .parameters = {Param::text(principal.tenant + ":" + principal.subject + ":" + key)}});
          if (!locked) {
            co_return std::unexpected(locked.error());
          }
        }
        auto lock = co_await tx->connection().query({.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                                     .parameters = {Param::text(principal.tenant + ":" + id)}});
        if (!lock) {
          co_return std::unexpected(lock.error());
        }
        bool replayed = false;
        if (operation != "get") {
          auto replay = co_await tx->connection().query(
              {.sql = "SELECT fingerprint FROM command.requests WHERE tenant=$1::uuid AND actor=$2 AND key=$3::uuid",
               .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key)}});
          if (!replay) {
            co_return std::unexpected(replay.error());
          }
          if (replay->size() > 0) {
            if (*replay->value(0, 0) != p::stream::sha256(operation + body.dump())) {
              co_return p::fail(Code::kCommonIdempotencyKeyReused, "request key already used");
            }
            replayed = true;
          }
        }
        auto previous = co_await load(tx, principal.tenant, id);
        Value value;
        if (operation == "preview"
            || (operation == "request" && !previous && previous.error().code() == Code::kCommonNotFound)) {
          auto input = parse<api::v1::RequestCommandRequest>(body);
          if (!input) {
            co_return std::unexpected(input.error());
          }
          if (!p::stream::uuid(input->device_id()) || input->capability().empty() || input->capability().size() > 128
              || input->parameters_size() > 64 || input->ByteSizeLong() > kRequestMaxBytes
              || (!input->incident_id().empty() && !p::stream::uuid(input->incident_id()))
              || (!input->run_id().empty() && !p::stream::uuid(input->run_id())) || input->step_id().size() > 128
              || input->run_id().empty() != input->step_id().empty()
              || (!input->run_id().empty() && input->incident_id().empty())) {
            co_return p::fail(Code::kCommonInvalidArgument, "invalid command fields");
          }
          for (const auto& [name, text] : input->parameters()) {
            if (name.empty() || name.size() > 128 || text.size() > 1024) {
              co_return p::fail(Code::kCommonInvalidArgument, "parameter exceeds limit");
            }
          }
          auto raw = co_await resource(tx, principal.tenant, "device", input->device_id());
          if (!raw) {
            co_return std::unexpected(raw.error());
          }
          auto device = parse<catalog::v1::Device>(*raw);
          if (!device) {
            co_return std::unexpected(device.error());
          }
          if (!p::stream::uuid(device->site_id()) || !p::stream::uuid(device->zone_id())
              || !p::stream::uuid(device->connector_id()) || !p::stream::uuid(device->device_type_id())
              || device->external_id().empty()) {
            co_return p::fail(Code::kCommonInvalidArgument, "invalid device configuration");
          }
          value.tenant = principal.tenant;
          value.site = device->site_id();
          value.zone = device->zone_id();
          value.external = device->external_id();
          value.fingerprint = p::stream::sha256(body.dump());
          auto& command = value.command;
          command.set_command_id(id);
          command.set_device_id(input->device_id());
          command.set_connector_id(device->connector_id());
          command.set_capability(input->capability());
          *command.mutable_parameters() = input->parameters();
          command.set_incident_id(input->incident_id());
          command.set_run_id(input->run_id());
          command.set_step_id(input->step_id());
          command.mutable_requested_by()->set_kind(
              principal.subject == "response-engine" ? common::v1::ACTOR_KIND_SERVICE : common::v1::ACTOR_KIND_USER);
          command.mutable_requested_by()->set_id(principal.subject);
          *command.mutable_requested_at() = Time::GetCurrentTime();
          *command.mutable_deadline_at() = command.requested_at();
          command.mutable_deadline_at()->set_seconds(command.requested_at().seconds() + timeout_);
          if (!command.incident_id().empty()) {
            auto parent = co_await resource(tx, principal.tenant, "incident", command.incident_id());
            if (!parent) {
              co_return std::unexpected(parent.error());
            }
            value.incident_site = parent->value("siteId", std::string{});
            value.incident_zone = parent->value("zoneId", std::string{});
            if (!p::stream::uuid(value.incident_site)
                || parent->value("state", std::string{}) == "INCIDENT_STATE_CLOSED") {
              co_return p::fail(Code::kCommandInvalidState, "incident unavailable for commands");
            }
          }
          auto scope = access(principal, action, value);
          if (!scope) {
            co_return std::unexpected(scope.error());
          }
          auto type_raw = co_await resource(tx, principal.tenant, "type", device->device_type_id());
          if (!type_raw) {
            co_return std::unexpected(type_raw.error());
          }
          auto type = parse<catalog::v1::DeviceType>(*type_raw);
          if (!type) {
            co_return std::unexpected(type.error());
          }
          auto supported = capability(*type, command);
          if (!supported) {
            co_return std::unexpected(supported.error());
          }
          const auto preview = preview_fingerprint(body, *device, *type);
          if (!input->preview_fingerprint().empty() && input->preview_fingerprint() != preview) {
            co_return p::fail(Code::kCommonConcurrentModification, "command preview configuration changed");
          }
          command.set_critical(*supported);
          command.set_state(*supported ? response::v1::COMMAND_STATE_AWAITING_APPROVAL
                                       : response::v1::COMMAND_STATE_REQUESTED);
          if (device->state() != catalog::v1::RESOURCE_STATE_ACTIVE) {
            co_return p::fail(Code::kCommandCapabilityUnsupported, "device disabled");
          }
          if (operation == "preview") {
            const auto definition =
                std::ranges::find(type->capabilities(), command.capability(), &catalog::v1::Capability::code);
            const auto consequence =
                definition == type->capabilities().end() ? std::string{} : definition->consequence();
            co_return Json{{"deviceId", device->device_id()},
                           {"capability", command.capability()},
                           {"consequence", consequence},
                           {"consequenceUnknown", consequence.empty()},
                           {"requiresSecondSupervisor", *supported},
                           {"deviceVersion", std::to_string(device->version())},
                           {"deviceTypeVersion", std::to_string(type->version())},
                           {"fingerprint", preview}};
          }
          if (!*supported) {
            auto sent = co_await dispatch(tx, &value);
            if (!sent) {
              co_return std::unexpected(sent.error());
            }
          }
        } else {
          if (!previous) {
            co_return std::unexpected(previous.error());
          }
          value = std::move(*previous);
          auto scope = access(principal, action, value);
          if (!scope) {
            co_return std::unexpected(scope.error());
          }
          if (operation == "get") {
            co_return Json{{"command", json(value.command)}};
          }
          if (operation == "request") {
            if (value.fingerprint != p::stream::sha256(body.dump())
                || value.command.requested_by().id() != principal.subject) {
              co_return p::fail(Code::kCommonIdempotencyKeyReused, "command identity already used");
            }
            co_return Json{{"command", json(value.command)}};
          }
          if (replayed) {
            co_return Json{{"command", json(value.command)}};
          }
          if (expected && value.command.version() != *expected) {
            co_return p::fail(Code::kCommonConcurrentModification, "command version differs");
          }
          auto change = d::Action::kCancel;
          if (operation == "approve") {
            change = d::Action::kApprove;
          } else if (operation == "reject") {
            if (body.value("reason", std::string{}).empty() || body.value("reason", std::string{}).size() > 4096) {
              co_return p::fail(Code::kCommonInvalidArgument, "rejection reason required");
            }
            change = d::Action::kReject;
          } else if (operation == "cancel") {
            change = d::Action::kCancel;
          } else {
            co_return p::fail(Code::kCommonNotFound, "unknown operation");
          }
          if (operation == "approve" && !body.value("previewFingerprint", std::string{}).empty()) {
            auto raw = co_await resource(tx, principal.tenant, "device", value.command.device_id());
            if (!raw) {
              co_return std::unexpected(raw.error());
            }
            auto device = parse<catalog::v1::Device>(*raw);
            if (!device) {
              co_return std::unexpected(device.error());
            }
            auto raw_type = co_await resource(tx, principal.tenant, "type", device->device_type_id());
            if (!raw_type) {
              co_return std::unexpected(raw_type.error());
            }
            auto type = parse<catalog::v1::DeviceType>(*raw_type);
            if (!type) {
              co_return std::unexpected(type.error());
            }
            api::v1::RequestCommandRequest proposed;
            proposed.set_command_id(value.command.command_id());
            proposed.set_device_id(value.command.device_id());
            proposed.set_capability(value.command.capability());
            *proposed.mutable_parameters() = value.command.parameters();
            proposed.set_incident_id(value.command.incident_id());
            proposed.set_run_id(value.command.run_id());
            proposed.set_step_id(value.command.step_id());
            if (body["previewFingerprint"] != preview_fingerprint(json(proposed), *device, *type)) {
              co_return p::fail(Code::kCommonConcurrentModification, "command preview configuration changed");
            }
          }
          auto changed = transition(value, change, principal.subject);
          if (!changed) {
            co_return std::unexpected(changed.error());
          }
          if (operation == "approve" && value.command.state() == response::v1::COMMAND_STATE_REQUESTED) {
            value.command.mutable_approved_by()->set_id(principal.subject);
            value.command.mutable_approved_by()->set_kind(common::v1::ACTOR_KIND_USER);
            auto sent = co_await dispatch(tx, &value);
            if (!sent) {
              co_return std::unexpected(sent.error());
            }
          }
          if (operation == "reject" && value.command.state() == response::v1::COMMAND_STATE_REJECTED) {
            value.command.set_result_message(body.value("reason", std::string{}));
          }
        }
        auto saved = co_await save(tx, value, principal, "command." + operation);
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        auto recorded = co_await tx->connection().query(
            {.sql = "INSERT INTO command.requests VALUES($1::uuid,$2,$3::uuid,$4)",
             .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key),
                            Param::text(p::stream::sha256(operation + body.dump()))}});
        if (!recorded) {
          co_return std::unexpected(recorded.error());
        }
        auto current = co_await load(tx, principal.tenant, id);
        if (!current) {
          co_return std::unexpected(current.error());
        }
        co_return Json{{"command", json(current->command)}};
      });
  if (!result
      && (result.error().code() == Code::kAuthForbidden || result.error().code() == Code::kAuthScopeDenied
          || result.error().code() == Code::kCommandApprovalSelf)) {
    auto denied = co_await pg::in_transaction<void>(
        pool_,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
        [this, &principal, &operation, &id](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          co_return co_await audit(tx, principal, "command." + operation, id, {}, true);
        });
    if (!denied) {
      co_return std::unexpected(denied.error());
    }
  }
  co_return result;
}

asio::awaitable<p::Result<bool>> Repository::result(response::v1::CommandResultRecord input) {
  const auto id =
      input.has_command_receipt() ? input.command_receipt().command_id() : input.command_outcome().command_id();
  if (!p::stream::uuid(id) || !p::stream::uuid(input.envelope().tenant_id())
      || !p::stream::uuid(input.envelope().message_id()) || !p::stream::uuid(input.session_id())
      || input.envelope().actor().kind() != common::v1::ACTOR_KIND_CONNECTOR
      || input.command_outcome().message().size() > 4096) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid command result");
  }
  co_return co_await pg::in_transaction<bool>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
      [this, &input, &id](pg::Transaction* tx) -> asio::awaitable<p::Result<bool>> {
        auto loaded = co_await load(tx, input.envelope().tenant_id(), id);
        if (!loaded) {
          if (loaded.error().code() == Code::kCommonNotFound) {
            co_return false;
          }
          co_return std::unexpected(loaded.error());
        }
        auto value = std::move(*loaded);
        if (value.command.connector_id() != input.envelope().actor().id() || value.session != input.session_id()) {
          co_return p::fail(Code::kProcessingPayloadInvalid, "result outside command session");
        }
        auto accepted =
            co_await messages_.accept(tx, "results", input.envelope().tenant_id() + ":" + input.envelope().message_id(),
                                      p::stream::sha256(input.SerializeAsString()));
        if (!accepted) {
          co_return std::unexpected(accepted.error());
        }
        if (!*accepted || d::terminal(static_cast<d::State>(value.command.state()))) {
          co_return false;
        }
        const auto before = value.command.state();
        auto action = d::Action::kFailure;
        if (input.has_command_receipt()) {
          action = d::Action::kReceipt;
        } else if (input.command_outcome().success()) {
          action = d::Action::kSuccess;
        }
        if (input.has_command_outcome() && !input.command_outcome().success()) {
          const auto& code = input.command_outcome().code();
          if (code == "COMMAND_INVALID_STATE" || code == "COMMAND_OUTCOME_UNKNOWN") {
            action = d::Action::kDisconnect;
          } else if (code == "COMMAND_EXPIRED" || code == "COMMAND_TIMED_OUT") {
            action = d::Action::kExpire;
          }
        }
        auto changed = transition(value, action);
        if (!changed) {
          co_return std::unexpected(changed.error());
        }
        if (value.command.state() == before) {
          co_return false;
        }
        if (value.command.state() == response::v1::COMMAND_STATE_FAILED) {
          value.command.set_result_code("COMMAND_DEVICE_REJECTED");
          value.command.set_result_message(input.command_outcome().message());
        }
        auto saved = co_await save(tx, value, system(value.tenant), "command.result");
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        co_return true;
      });
}

asio::awaitable<p::Result<void>> Repository::tick() {
  std::vector<std::pair<std::string, std::string>> pending;
  {
    auto lease = co_await pool_->acquire();
    if (!lease) {
      co_return std::unexpected(lease.error());
    }
    auto rows = co_await (*lease)->query(
        {.sql = "SELECT tenant::text,id::text FROM command.commands WHERE body->>'state' IN "
                "('COMMAND_STATE_REQUESTED','COMMAND_STATE_AWAITING_APPROVAL','COMMAND_STATE_SENT','COMMAND_STATE_"
                "ACKNOWLEDGED') ORDER BY (body->>'deadlineAt') LIMIT 1000"});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    for (std::size_t index = 0; index < rows->size(); ++index) {
      pending.emplace_back(rows->value(index, 0).value_or(""), rows->value(index, 1).value_or(""));
    }
  }
  for (const auto& [tenant, id] : pending) {
    auto updated = co_await pg::in_transaction<void>(
        pool_,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
        [this, &tenant, &id](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          auto loaded = co_await load(tx, tenant, id);
          if (!loaded) {
            co_return std::unexpected(loaded.error());
          }
          auto value = std::move(*loaded);
          if (d::terminal(static_cast<d::State>(value.command.state()))) {
            co_return p::Result<void>{};
          }
          if (Time::TimestampToNanoseconds(value.command.deadline_at())
              <= Time::TimestampToNanoseconds(Time::GetCurrentTime())) {
            auto changed = transition(value, d::Action::kExpire);
            if (!changed) {
              co_return changed;
            }
          } else if (!value.session.empty()) {
            auto session = co_await sessions_(tenant, value.command.connector_id());
            if (!session) {
              co_return std::unexpected(session.error());
            }
            if (*session && **session == value.session) {
              co_return p::Result<void>{};
            }
            auto changed = transition(value, d::Action::kDisconnect);
            if (!changed) {
              co_return changed;
            }
          } else {
            co_return p::Result<void>{};
          }
          co_return co_await save(tx, value, system(tenant), "command.finalize");
        });
    if (!updated) {
      co_return updated;
    }
  }
  co_return p::Result<void>{};
}
}  // namespace psim::command_service
