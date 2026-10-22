#include "registry.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/connector/v1/administration.pb.h>
#include <psim/ingest/v1/state.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "protocol.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::connector_gateway {
namespace p = platform;
namespace pg = p::pg;
namespace asio = boost::asio;
using Param = pg::Parameter;
using Util = google::protobuf::util::TimeUtil;

void envelope(common::v1::Envelope* output, std::string tenant, std::string connector, std::string id) {
  output->set_message_id(id.empty() ? p::stream::uuid7() : std::move(id));
  output->set_tenant_id(std::move(tenant));
  output->set_correlation_id(output->message_id());
  *output->mutable_occurred_at() = Util::GetCurrentTime();
  *output->mutable_received_at() = output->occurred_at();
  *output->mutable_produced_at() = output->occurred_at();
  output->set_schema_version("1");
  output->mutable_actor()->set_kind(common::v1::ACTOR_KIND_CONNECTOR);
  output->mutable_actor()->set_id(std::move(connector));
}

asio::awaitable<p::Result<void>> Registry::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto initialized = co_await messages_.initialize(&**lease);
  if (!initialized) {
    co_return initialized;
  }
  auto migrated = co_await pg::Migrator("ingest").apply(
      &**lease,
      {{.version = 1,
        .name = "connector registrations and sessions",
        .expand =
            {"CREATE TABLE ingest.control (id integer PRIMARY KEY CHECK(id=1), revision bigint NOT NULL)",
             "INSERT INTO ingest.control VALUES(1,0)",
             ("CREATE TABLE ingest.registrations (connector_id uuid PRIMARY KEY, tenant_id uuid NOT NULL, body "
              "jsonb NOT NULL)"),
             ("CREATE TABLE ingest.revoked (connector_id uuid NOT NULL, fingerprint text NOT NULL, PRIMARY "
              "KEY(connector_id,fingerprint))"),
             ("CREATE TABLE ingest.requests (tenant_id uuid NOT NULL, actor text NOT NULL, request_id uuid NOT "
              "NULL, fingerprint text NOT NULL, response jsonb NOT NULL, PRIMARY KEY(tenant_id,actor,request_id))"),
             ("CREATE TABLE ingest.gateways (boot uuid PRIMARY KEY, revision bigint NOT NULL, valid_until "
              "timestamptz NOT NULL)"),
             ("CREATE TABLE ingest.sessions (connector_id uuid PRIMARY KEY, tenant_id uuid NOT NULL, session_id "
              "uuid NOT NULL UNIQUE, online boolean NOT NULL, expires_at timestamptz NOT NULL)"),
             "CREATE INDEX ingest_session_expiry ON ingest.sessions(expires_at) WHERE online"}},
       {.version = 2,
        .name = "connector connection versions",
        .expand = {"ALTER TABLE ingest.sessions ADD COLUMN version bigint NOT NULL DEFAULT 0"}},
       {.version = 3,
        .name = "connector catalog publication",
        .expand = {"ALTER TABLE ingest.registrations ADD COLUMN catalog_version bigint NOT NULL DEFAULT 0"}}});
  if (!migrated) {
    co_return migrated;
  }
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [this](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto locked =
            co_await tx->connection().query({.sql = "SELECT revision FROM ingest.control WHERE id=1 FOR UPDATE"});
        if (!locked) {
          co_return std::unexpected(locked.error());
        }
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT tenant_id::text,body::text FROM ingest.registrations WHERE "
                    "catalog_version<(body->>'version')::bigint ORDER BY connector_id"});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        for (std::size_t row = 0; row < rows->size(); ++row) {
          catalog::v1::Connector value;
          if (!google::protobuf::util::JsonStringToMessage(*rows->value(row, 1), &value).ok()) {
            co_return p::fail(p::ErrorCode::kCommonInternal, "invalid persisted connector");
          }
          auto result = co_await publish_catalog(tx, std::string(*rows->value(row, 0)), value, "connector-gateway");
          if (!result) {
            co_return result;
          }
        }
        co_return p::Result<void>{};
      });
}

asio::awaitable<p::Result<Snapshot>> Registry::snapshot() {
  co_return co_await pg::in_transaction<Snapshot>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [](pg::Transaction* tx) -> asio::awaitable<p::Result<Snapshot>> {
        auto revision = co_await tx->connection().query({.sql = "SELECT revision FROM ingest.control WHERE id=1"});
        if (!revision) {
          co_return std::unexpected(revision.error());
        }
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT tenant_id::text,body::text FROM ingest.registrations ORDER BY connector_id LIMIT 10001"});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        if (rows->size() > 10000) {
          co_return p::fail(p::ErrorCode::kCommonRateLimited, "registration capacity exceeded");
        }
        Snapshot snapshot;
        auto value = revision->integer(0, 0);
        if (!value || *value < 0) {
          co_return p::fail(p::ErrorCode::kCommonInternal, "invalid registry revision");
        }
        snapshot.revision = static_cast<std::uint64_t>(*value);
        for (std::size_t i = 0; i < rows->size(); ++i) {
          Registration registration{.tenant = std::string(*rows->value(i, 0))};
          if (!google::protobuf::util::JsonStringToMessage(std::string(*rows->value(i, 1)), &registration.connector)
                   .ok()) {
            co_return p::fail(p::ErrorCode::kCommonInternal, "invalid stored registration");
          }
          snapshot.registrations.emplace(registration.connector.connector_id(), std::move(registration));
        }
        co_return snapshot;
      },
      pg::Isolation::kRepeatableRead);
}

asio::awaitable<p::Result<connector::v1::ManageResponse>> Registry::manage(connector::v1::ManageRequest request,
                                                                           p::security::Principal principal,
                                                                           std::optional<Certificate> cert) {
  using namespace connector::v1;
  if (!p::stream::uuid(request.request_id()) || !p::stream::uuid(request.connector().connector_id())
      || !p::stream::uuid(principal.tenant) || principal.subject.empty() || principal.subject.size() > 256
      || (!principal.roles.contains("admin") && !principal.roles.contains("integrator"))
      || request.action() == REGISTRATION_ACTION_UNSPECIFIED || !RegistrationAction_IsValid(request.action())
      || (request.action() == REGISTRATION_ACTION_RECOVER
          && (!principal.roles.contains("admin") || principal.authorized_party != "psim-console"))) {
    if (p::stream::uuid(principal.tenant) && !principal.subject.empty()) {
      auto recorded =
          co_await denied(principal.tenant, principal.subject, request.connector().connector_id(), "connector.manage");
      if (!recorded) {
        co_return std::unexpected(recorded.error());
      }
    }
    co_return p::fail(p::ErrorCode::kAuthForbidden, "registration operation denied");
  }
  auto managed = co_await pg::in_transaction<ManageResponse>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned by in_transaction
      [this, &request, &principal, &cert](pg::Transaction* tx) -> asio::awaitable<p::Result<ManageResponse>> {
        auto& db = tx->connection();
        auto lock = co_await db.query({.sql = "SELECT revision FROM ingest.control WHERE id=1 FOR UPDATE"});
        if (!lock) {
          co_return std::unexpected(lock.error());
        }
        const auto hash = p::stream::sha256(request.SerializeAsString());
        auto previous = co_await db.query({.sql = "SELECT fingerprint,response::text FROM ingest.requests WHERE "
                                                  "tenant_id=$1::uuid AND actor=$2 AND request_id=$3::uuid",
                                           .parameters = {Param::text(principal.tenant), Param::text(principal.subject),
                                                          Param::text(request.request_id())}});
        if (!previous) {
          co_return std::unexpected(previous.error());
        }
        if (previous->size() != 0) {
          ManageResponse response;
          if (previous->value(0, 0) != hash
              || !google::protobuf::util::JsonStringToMessage(std::string(*previous->value(0, 1)), &response).ok()) {
            co_return p::fail(p::ErrorCode::kCommonConcurrentModification,
                              "request identity already has a different payload");
          }
          co_return response;
        }
        const auto id = request.connector().connector_id();
        auto rows = co_await db.query(
            {.sql = "SELECT tenant_id::text,body::text FROM ingest.registrations WHERE connector_id=$1::uuid",
             .parameters = {Param::text(id)}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        const bool create = request.action() == REGISTRATION_ACTION_REGISTER;
        catalog::v1::Connector value;
        if (create) {
          if (rows->size() != 0 || request.expected_version() != 0) {
            co_return p::fail(p::ErrorCode::kCommonConcurrentModification, "connector is already registered");
          }
          auto count = co_await db.query({.sql = "SELECT count(*) FROM ingest.registrations"});
          if (!count) {
            co_return std::unexpected(count.error());
          }
          auto number = count->integer(0, 0);
          if (!number || *number >= 10000) {
            co_return p::fail(p::ErrorCode::kCommonRateLimited, "registration capacity reached");
          }
          value = request.connector();
          value.clear_certificates();
          value.set_version(0);
          value.set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
          if (value.name().empty() || value.name().size() > 200 || value.connector_type().empty()
              || value.connector_type().size() > 128) {
            co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid connector name or type");
          }
          if (value.limits().max_batch_events() == 0) {
            value.mutable_limits()->set_max_batch_events(1000);
          }
          if (value.limits().max_events_per_second() == 0) {
            value.mutable_limits()->set_max_events_per_second(100000);
          }
          if (value.limits().max_batch_events() > 1000 || value.limits().max_events_per_second() > 1000000) {
            co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid connector limits");
          }
        } else {
          if (rows->size() == 0 || rows->value(0, 0) != principal.tenant) {
            co_return p::fail(p::ErrorCode::kAuthForbidden, "connector outside operator tenant");
          }
          if (!google::protobuf::util::JsonStringToMessage(std::string(*rows->value(0, 1)), &value).ok()) {
            co_return p::fail(p::ErrorCode::kCommonInternal, "invalid stored connector");
          }
          if (request.action() == REGISTRATION_ACTION_INSPECT) {
            auto revision = lock->integer(0, 0);
            if (!revision) {
              co_return std::unexpected(revision.error());
            }
            ManageResponse response;
            *response.mutable_connector() = value;
            response.set_revision(static_cast<std::uint64_t>(*revision));
            co_return response;
          }
          if (value.version() != request.expected_version()) {
            co_return p::fail(p::ErrorCode::kCommonConcurrentModification, "connector version changed");
          }
        }
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure stays alive
        auto revoke = [&](std::string fingerprint) -> asio::awaitable<p::Result<void>> {
          auto saved = co_await db.query(
              {.sql = "INSERT INTO ingest.revoked(connector_id,fingerprint) VALUES($1::uuid,$2) ON CONFLICT DO NOTHING",
               .parameters = {Param::text(id), Param::text(std::move(fingerprint))}});
          if (!saved) {
            co_return std::unexpected(saved.error());
          }
          co_return p::Result<void>{};
        };
        if (request.action() == REGISTRATION_ACTION_REVOKE) {
          if (request.fingerprint().size() != 64 || !std::ranges::all_of(request.fingerprint(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
              })) {
            co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "SHA-256 fingerprint required");
          }
          auto saved = co_await revoke(request.fingerprint());
          if (!saved) {
            co_return std::unexpected(saved.error());
          }
          auto* certificates = value.mutable_certificates();
          for (int i = certificates->size() - 1; i >= 0; --i) {
            if (certificates->Get(i).sha256() == request.fingerprint()) {
              certificates->DeleteSubrange(i, 1);
            }
          }
        } else if (request.action() == REGISTRATION_ACTION_DISABLE) {
          value.set_state(catalog::v1::RESOURCE_STATE_DISABLED);
        } else {
          if (!cert || cert->identity != "psim://connector/" + id
              || cert->expires_at <= std::chrono::system_clock::now()) {
            co_return p::fail(p::ErrorCode::kAuthUnauthenticated, "certificate identity differs from connector");
          }
          if ((!request.expected_fingerprint().empty() && request.expected_fingerprint() != cert->fingerprint)
              || request.reason().size() > 2000) {
            co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "certificate fingerprint or reason is invalid");
          }
          if (request.has_previous_not_after()) {
            const auto& until = request.previous_not_after();
            const auto current = Util::GetCurrentTime();
            if (request.action() != REGISTRATION_ACTION_ROTATE || until.seconds() < current.seconds()
                || (until.seconds() == current.seconds() && until.nanos() <= current.nanos()) || until.nanos() < 0
                || until.nanos() >= 1000000000) {
              co_return p::fail(p::ErrorCode::kCommonInvalidArgument,
                                "previous certificate expiry must be in the future");
            }
            for (auto& prior_certificate : *value.mutable_certificates()) {
              if (until.seconds() > prior_certificate.not_after().seconds()
                  || (until.seconds() == prior_certificate.not_after().seconds()
                      && until.nanos() > prior_certificate.not_after().nanos())) {
                co_return p::fail(p::ErrorCode::kCommonInvalidArgument,
                                  "previous certificate validity cannot be extended");
              }
              *prior_certificate.mutable_not_after() = until;
            }
          }
          auto used = co_await db.query(
              {.sql = "SELECT fingerprint FROM ingest.revoked WHERE connector_id=$1::uuid AND fingerprint=$2",
               .parameters = {Param::text(id), Param::text(cert->fingerprint)}});
          if (!used) {
            co_return std::unexpected(used.error());
          }
          if (used->size() != 0 || std::ranges::any_of(value.certificates(), [&](const auto& item) {
                return item.sha256() == cert->fingerprint;
              })) {
            co_return p::fail(p::ErrorCode::kAuthForbidden, "fresh certificate required");
          }
          if (request.action() == REGISTRATION_ACTION_RECOVER) {
            for (const auto& item : value.certificates()) {
              auto saved = co_await revoke(item.sha256());
              if (!saved) {
                co_return std::unexpected(saved.error());
              }
            }
            value.clear_certificates();
            value.set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
          }
          auto* certificates = value.mutable_certificates();
          const auto now = Util::GetCurrentTime().seconds();
          for (int i = certificates->size() - 1; i >= 0; --i) {
            if (certificates->Get(i).not_after().seconds() <= now) {
              certificates->DeleteSubrange(i, 1);
            }
          }
          if (certificates->size() >= 2) {
            co_return p::fail(p::ErrorCode::kCatalogCertificateLimit, "finish certificate rotation first");
          }
          auto* item = value.add_certificates();
          item->set_sha256(cert->fingerprint);
          *item->mutable_not_after() = Util::SecondsToTimestamp(
              std::chrono::floor<std::chrono::seconds>(cert->expires_at.time_since_epoch()).count());
        }
        value.set_version(value.version() + 1);
        auto saved = co_await db.query(
            {.sql = "INSERT INTO ingest.registrations(connector_id,tenant_id,body) VALUES($1::uuid,$2::uuid,$3::jsonb) "
                    "ON CONFLICT(connector_id) DO UPDATE SET body=EXCLUDED.body",
             .parameters = {Param::text(id), Param::text(principal.tenant), Param::text(p::stream::json(value))}});
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        auto revised =
            co_await db.query({.sql = "UPDATE ingest.control SET revision=revision+1 WHERE id=1 RETURNING revision"});
        if (!revised) {
          co_return std::unexpected(revised.error());
        }
        auto revision = revised->integer(0, 0);
        if (!revision) {
          co_return std::unexpected(revision.error());
        }
        ManageResponse response;
        *response.mutable_connector() = value;
        response.set_revision(static_cast<std::uint64_t>(*revision));
        auto recorded =
            co_await db.query({.sql = "INSERT INTO ingest.requests(tenant_id,actor,request_id,fingerprint,response) "
                                      "VALUES($1::uuid,$2,$3::uuid,$4,$5::jsonb)",
                               .parameters = {Param::text(principal.tenant), Param::text(principal.subject),
                                              Param::text(request.request_id()), Param::text(hash),
                                              Param::text(p::stream::json(response))}});
        if (!recorded) {
          co_return std::unexpected(recorded.error());
        }
        audit::v1::AuditEventRecord audit;
        envelope(audit.mutable_envelope(), principal.tenant, id);
        audit.mutable_envelope()->set_causation_id(audit.envelope().message_id());
        audit.mutable_envelope()->mutable_actor()->set_kind(common::v1::ACTOR_KIND_USER);
        audit.mutable_envelope()->mutable_actor()->set_id(principal.subject);
        auto* entry = audit.mutable_audit_entry();
        *entry->mutable_actor() = audit.envelope().actor();
        entry->set_action("connector." + RegistrationAction_Name(request.action()));
        entry->set_object_type("connector");
        entry->set_object_id(id);
        entry->set_outcome(audit::v1::AUDIT_OUTCOME_SUCCESS);
        (*entry->mutable_details())["version"] = std::to_string(value.version());
        (*entry->mutable_details())["request_id"] = request.request_id();
        if (!request.reason().empty()) {
          (*entry->mutable_details())["reason"] = request.reason();
        }
        auto appended = co_await messages_.append(
            tx, {audit.envelope().message_id(), encode_("psim.audit.v1", principal.tenant, audit)});
        if (!appended) {
          co_return std::unexpected(appended.error());
        }
        auto catalogued = co_await publish_catalog(tx, principal.tenant, value, principal.subject);
        if (!catalogued) {
          co_return std::unexpected(catalogued.error());
        }
        co_return response;
      });
  if (!managed && managed.error().code() == p::ErrorCode::kAuthForbidden) {
    auto recorded =
        co_await denied(principal.tenant, principal.subject, request.connector().connector_id(), "connector.manage");
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
  }
  co_return managed;
}

asio::awaitable<p::Result<void>> Registry::publish_catalog(pg::Transaction* tx, std::string tenant,
                                                           catalog::v1::Connector connector, std::string actor) {
  catalog::v1::CatalogRecord record;
  envelope(record.mutable_envelope(), tenant, connector.connector_id());
  record.mutable_envelope()->set_sequence(connector.version());
  record.mutable_envelope()->mutable_actor()->set_kind(actor == "connector-gateway" ? common::v1::ACTOR_KIND_SERVICE
                                                                                    : common::v1::ACTOR_KIND_USER);
  record.mutable_envelope()->mutable_actor()->set_id(std::move(actor));
  *record.mutable_connector() = connector;
  auto appended = co_await messages_.append(
      tx,
      {record.envelope().message_id(), encode_("psim.catalog.v1", "connector:" + connector.connector_id(), record)});
  if (!appended) {
    co_return appended;
  }
  auto updated = co_await tx->connection().query(
      {.sql = "UPDATE ingest.registrations SET catalog_version=$2::bigint WHERE connector_id=$1::uuid",
       .parameters = {Param::text(connector.connector_id()),
                      Param::integer(static_cast<std::int64_t>(connector.version()))}});
  if (!updated) {
    co_return std::unexpected(updated.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Registry::acknowledge(std::string boot, std::uint64_t revision) {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto saved = co_await (*lease)->query(
      {.sql = "INSERT INTO ingest.gateways(boot,revision,valid_until) VALUES($1::uuid,$2,clock_timestamp()+interval '3 "
              "seconds') ON CONFLICT(boot) DO UPDATE SET revision=EXCLUDED.revision,valid_until=EXCLUDED.valid_until",
       .parameters = {Param::text(std::move(boot)), Param::integer(static_cast<std::int64_t>(revision))}});
  if (!saved) {
    co_return std::unexpected(saved.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<bool>> Registry::enforced(std::uint64_t revision) {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows = co_await (*lease)->query(
      {.sql = "SELECT count(*) FROM ingest.gateways WHERE valid_until>clock_timestamp() AND revision<$1",
       .parameters = {Param::integer(static_cast<std::int64_t>(revision))}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  auto count = rows->integer(0, 0);
  if (!count) {
    co_return std::unexpected(count.error());
  }
  co_return *count == 0;
}

asio::awaitable<p::Result<void>> Registry::denied(std::string tenant, std::string actor, std::string object,
                                                  std::string action) {
  audit::v1::AuditEventRecord record;
  envelope(record.mutable_envelope(), tenant, object);
  record.mutable_envelope()->mutable_actor()->set_id("connector-gateway");
  record.mutable_envelope()->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
  auto* entry = record.mutable_audit_entry();
  entry->mutable_actor()->set_id(actor);
  entry->mutable_actor()->set_kind(action == "connector.session" ? common::v1::ACTOR_KIND_CONNECTOR
                                                                 : common::v1::ACTOR_KIND_USER);
  entry->set_action("access.denied");
  entry->set_object_type("connector");
  entry->set_object_id(object);
  entry->set_outcome(audit::v1::AUDIT_OUTCOME_DENIED);
  (*entry->mutable_details())["requested_action"] = action;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): in_transaction owns the awaited closure.
  co_return co_await pg::in_transaction<void>(pool_, [&, this](pg::Transaction* tx) {
    return messages_.append(tx, {record.envelope().message_id(), encode_("psim.audit.v1", tenant, record)});
  });
}

asio::awaitable<p::Result<void>> Registry::append(pg::Transaction* tx, const ingest::v1::GatewayStateRecord* state) {
  auto saved = co_await messages_.append(
      tx, {state->envelope().message_id(), encode_("psim.ingest.state.v1", state->connector_id(), *state)});
  if (!saved) {
    co_return saved;
  }
  audit::v1::AuditEventRecord record;
  envelope(record.mutable_envelope(), state->envelope().tenant_id(), state->connector_id());
  record.mutable_envelope()->set_causation_id(state->envelope().message_id());
  record.mutable_envelope()->mutable_actor()->set_id("connector-gateway");
  record.mutable_envelope()->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
  auto* entry = record.mutable_audit_entry();
  *entry->mutable_actor() = record.envelope().actor();
  entry->set_action(state->connection().online() ? "connector.connected" : "connector.disconnected");
  entry->set_object_type("connector");
  entry->set_object_id(state->connector_id());
  entry->set_outcome(audit::v1::AUDIT_OUTCOME_SUCCESS);
  (*entry->mutable_details())["session_id"] = state->session_id();
  (*entry->mutable_details())["reason"] = state->connection().reason();
  co_return co_await messages_.append(
      tx, {record.envelope().message_id(), encode_("psim.audit.v1", record.envelope().tenant_id(), record)});
}

asio::awaitable<p::Result<void>> Registry::connected(Registration registration, Certificate peer, std::string session,
                                                     int ttl) {
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned by in_transaction
      [this, &registration, &peer, &session, ttl](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto current = co_await tx->connection().query(
            {.sql =
                 "SELECT tenant_id::text,body::text FROM ingest.registrations WHERE connector_id=$1::uuid FOR UPDATE",
             .parameters = {Param::text(registration.connector.connector_id())}});
        if (!current) {
          co_return std::unexpected(current.error());
        }
        Registration verified;
        if (current->size() != 1
            || !google::protobuf::util::JsonStringToMessage(std::string(*current->value(0, 1)), &verified.connector)
                    .ok()) {
          co_return p::fail(p::ErrorCode::kAuthUnauthenticated, "connector registration unavailable");
        }
        verified.tenant = std::string(*current->value(0, 0));
        if (verified.tenant != registration.tenant || !authorized(verified, peer, std::chrono::system_clock::now())) {
          co_return p::fail(p::ErrorCode::kAuthUnauthenticated, "connector registration changed");
        }
        auto owner =
            co_await tx->connection().query({.sql = "SELECT session_id FROM ingest.sessions WHERE "
                                                    "connector_id=$1::uuid AND online AND expires_at>clock_timestamp()",
                                             .parameters = {Param::text(registration.connector.connector_id())}});
        if (!owner) {
          co_return std::unexpected(owner.error());
        }
        if (owner->size() != 0) {
          co_return p::fail(p::ErrorCode::kCommonUnavailable, "connector still has an authoritative session lease");
        }
        auto expired = co_await tx->connection().query(
            {.sql = "UPDATE ingest.sessions SET online=false,version=version+1 WHERE connector_id=$1::uuid "
                    "AND online AND expires_at<=clock_timestamp() RETURNING session_id::text,version",
             .parameters = {Param::text(registration.connector.connector_id())}});
        if (!expired) {
          co_return std::unexpected(expired.error());
        }
        if (expired->size() != 0) {
          ingest::v1::GatewayStateRecord previous;
          envelope(previous.mutable_envelope(), registration.tenant, registration.connector.connector_id());
          auto version = expired->integer(0, 1);
          if (!version) {
            co_return std::unexpected(version.error());
          }
          previous.mutable_envelope()->set_sequence(static_cast<std::uint64_t>(*version));
          previous.set_connector_id(registration.connector.connector_id());
          previous.set_session_id(std::string(*expired->value(0, 0)));
          previous.mutable_connection()->set_online(false);
          previous.mutable_connection()->set_quality("stale");
          previous.mutable_connection()->set_reason("lease_expired");
          auto appended = co_await append(tx, &previous);
          if (!appended) {
            co_return appended;
          }
        }
        auto saved = co_await tx->connection().query(
            {.sql = "INSERT INTO ingest.sessions(connector_id,tenant_id,session_id,online,expires_at,version) "
                    "VALUES($1::uuid,$2::uuid,$3::uuid,true,clock_timestamp()+$4*interval '1 second',1) ON "
                    "CONFLICT(connector_id) DO UPDATE SET "
                    "session_id=EXCLUDED.session_id,online=true,expires_at=EXCLUDED.expires_at,version=ingest.sessions."
                    "version+1 "
                    "RETURNING version",
             .parameters = {Param::text(registration.connector.connector_id()), Param::text(registration.tenant),
                            Param::text(session), Param::integer(ttl)}});
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        ingest::v1::GatewayStateRecord state;
        envelope(state.mutable_envelope(), registration.tenant, registration.connector.connector_id());
        auto version = saved->integer(0, 0);
        if (!version) {
          co_return std::unexpected(version.error());
        }
        state.mutable_envelope()->set_sequence(static_cast<std::uint64_t>(*version));
        state.set_connector_id(registration.connector.connector_id());
        state.set_session_id(session);
        state.mutable_connection()->set_online(true);
        state.mutable_connection()->set_quality("unknown");
        state.mutable_connection()->set_reason("connected");
        co_return co_await append(tx, &state);
      });
}

asio::awaitable<p::Result<bool>> Registry::owns(std::string session) {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows = co_await (*lease)->query({.sql = "SELECT session_id FROM ingest.sessions WHERE session_id=$1::uuid AND "
                                               "online AND expires_at>clock_timestamp()",
                                        .parameters = {Param::text(std::move(session))}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  co_return rows->size() == 1;
}

asio::awaitable<p::Result<bool>> Registry::renew(std::string session, int ttl) {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows =
      co_await (*lease)->query({.sql = "UPDATE ingest.sessions SET expires_at=clock_timestamp()+$2*interval '1 "
                                       "second' WHERE session_id=$1::uuid AND online AND expires_at>clock_timestamp()",
                                .parameters = {Param::text(std::move(session)), Param::integer(ttl)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  co_return rows->affected() == 1;
}

asio::awaitable<p::Result<void>> Registry::disconnected(std::string session, std::string reason) {
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned by in_transaction
      [this, &session, &reason](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto rows = co_await tx->connection().query(
            {.sql = "UPDATE ingest.sessions SET online=false,version=version+1 WHERE session_id=$1::uuid "
                    "AND online RETURNING tenant_id::text,connector_id::text,version",
             .parameters = {Param::text(session)}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        if (rows->size() == 0) {
          co_return p::Result<void>{};
        }
        ingest::v1::GatewayStateRecord state;
        envelope(state.mutable_envelope(), std::string(*rows->value(0, 0)), std::string(*rows->value(0, 1)));
        auto version = rows->integer(0, 2);
        if (!version) {
          co_return std::unexpected(version.error());
        }
        state.mutable_envelope()->set_sequence(static_cast<std::uint64_t>(*version));
        state.set_connector_id(std::string(*rows->value(0, 1)));
        state.set_session_id(session);
        state.mutable_connection()->set_online(false);
        state.mutable_connection()->set_quality("stale");
        state.mutable_connection()->set_reason(reason);
        co_return co_await append(tx, &state);
      });
}

asio::awaitable<p::Result<void>> Registry::expire() {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is owned by in_transaction
  co_return co_await pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [this](pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto rows =
            co_await tx->connection().query({.sql = "UPDATE ingest.sessions SET online=false,version=version+1 "
                                                    "WHERE online AND expires_at<=clock_timestamp() RETURNING "
                                                    "tenant_id::text,connector_id::text,session_id::text,version"});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        for (std::size_t i = 0; i < rows->size(); ++i) {
          ingest::v1::GatewayStateRecord state;
          envelope(state.mutable_envelope(), std::string(*rows->value(i, 0)), std::string(*rows->value(i, 1)));
          auto version = rows->integer(i, 3);
          if (!version) {
            co_return std::unexpected(version.error());
          }
          state.mutable_envelope()->set_sequence(static_cast<std::uint64_t>(*version));
          state.set_connector_id(std::string(*rows->value(i, 1)));
          state.set_session_id(std::string(*rows->value(i, 2)));
          state.mutable_connection()->set_online(false);
          state.mutable_connection()->set_quality("stale");
          state.mutable_connection()->set_reason("lease_expired");
          auto saved = co_await append(tx, &state);
          if (!saved) {
            co_return saved;
          }
        }
        co_return p::Result<void>{};
      });
}
}  // namespace psim::connector_gateway
