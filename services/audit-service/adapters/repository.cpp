// NOLINTBEGIN(cppcoreguidelines-avoid-capturing-lambda-coroutines): in_transaction owns the awaited closure.
#include "repository.hpp"

#include <google/protobuf/util/time_util.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/common/v1/envelope.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "chain.hpp"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::audit_service {
namespace {
using Param = p::pg::Parameter;
using Code = p::ErrorCode;
using Time = google::protobuf::util::TimeUtil;

asio::awaitable<p::Result<void>> execute(p::pg::Connection* connection, std::string sql,
                                         std::vector<Param> parameters = {}) {
  auto result = co_await connection->query({.sql = std::move(sql), .parameters = std::move(parameters)});
  if (!result) {
    co_return std::unexpected(result.error());
  }
  co_return p::Result<void>{};
}

audit::v1::AuditEventRecord event(std::string tenant, std::string actor, std::string action, bool denied,
                                  const Json& details) {
  audit::v1::AuditEventRecord value;
  auto* origin = value.mutable_envelope();
  origin->set_message_id(p::stream::uuid7());
  origin->set_tenant_id(std::move(tenant));
  origin->set_schema_version("1");
  origin->set_correlation_id(origin->message_id());
  *origin->mutable_occurred_at() = Time::GetCurrentTime();
  *origin->mutable_produced_at() = origin->occurred_at();
  origin->mutable_actor()->set_kind(common::v1::ACTOR_KIND_SERVICE);
  origin->mutable_actor()->set_id("audit-service");
  auto* entry = value.mutable_audit_entry();
  entry->mutable_actor()->set_kind(common::v1::ACTOR_KIND_USER);
  entry->mutable_actor()->set_id(std::move(actor));
  entry->set_action(std::move(action));
  entry->set_object_type("audit");
  entry->set_outcome(denied ? audit::v1::AUDIT_OUTCOME_DENIED : audit::v1::AUDIT_OUTCOME_SUCCESS);
  if (entry->action() == "audit.integrity.violated") {
    entry->set_outcome(audit::v1::AUDIT_OUTCOME_FAILED);
  }
  for (const auto& item : details.items()) {
    (*entry->mutable_details())[item.key()] = item.value().get<std::string>();
  }
  return value;
}

std::uint64_t number(const Json& body, const std::string& name) {
  const auto found = body.find(name);
  if (found == body.end()) {
    return 0;
  }
  return found->is_string() ? std::stoull(found->get<std::string>()) : found->get<std::uint64_t>();
}
}  // namespace

asio::awaitable<p::Result<void>> migrate(p::pg::Connection* connection, std::string role) {
  if (role.empty() || !std::ranges::all_of(role, [](char ch) {
        return (ch >= 'a' && ch <= 'z') || ch == '_' || (ch >= '0' && ch <= '9');
      })) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid append role");
  }
  auto result = co_await p::pg::MessageStore("audit").initialize(connection);
  if (!result) {
    co_return result;
  }
  result = co_await p::pg::Migrator("audit").apply(
      connection,
      {{.version = 1,
        .name = "append-only tenant audit chains",
        .expand =
            {R"sql(CREATE TABLE audit.entries(tenant uuid NOT NULL,seq bigint NOT NULL CHECK(seq>0),source text NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,seq),UNIQUE(tenant,source)))sql",
             R"sql(CREATE INDEX audit_entry_actor ON audit.entries(tenant,(body->'entry'->'actor'->>'id'),seq DESC))sql",
             R"sql(CREATE INDEX audit_entry_action ON audit.entries(tenant,(body->'entry'->>'action'),seq DESC))sql",
             R"sql(CREATE TABLE audit.checkpoints(tenant uuid NOT NULL,seq bigint NOT NULL,body jsonb NOT NULL,PRIMARY KEY(tenant,seq)))sql",
             R"sql(CREATE FUNCTION audit.reject_mutation() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'audit records are append only' USING ERRCODE='42501'; END $$)sql",
             R"sql(CREATE TRIGGER immutable_entries BEFORE UPDATE OR DELETE OR TRUNCATE ON audit.entries FOR EACH STATEMENT EXECUTE FUNCTION audit.reject_mutation())sql",
             R"sql(CREATE TRIGGER immutable_checkpoints BEFORE UPDATE OR DELETE OR TRUNCATE ON audit.checkpoints FOR EACH STATEMENT EXECUTE FUNCTION audit.reject_mutation())sql",
             R"sql(REVOKE ALL ON audit.entries,audit.checkpoints FROM PUBLIC)sql",
             R"sql(REVOKE CREATE ON SCHEMA audit FROM PUBLIC)sql"}},
       {.version = 2,
        .name = "identity journal cursors",
        .expand = {"CREATE TABLE audit.identity_cursors(source text PRIMARY KEY,watermark bigint NOT NULL)"}},
       {.version = 3,
        .name = "bounded identity journal pages",
        .expand = {"ALTER TABLE audit.identity_cursors ADD COLUMN page integer NOT NULL DEFAULT 0,ADD COLUMN cutoff "
                   "bigint NOT NULL DEFAULT 0"}}});
  if (!result) {
    co_return result;
  }
  for (const auto& sql : {"GRANT USAGE ON SCHEMA audit TO " + role,
                          "GRANT SELECT,INSERT ON audit.entries,audit.checkpoints,audit.psim_inbox TO " + role,
                          "GRANT SELECT,INSERT,UPDATE ON audit.psim_outbox,audit.identity_cursors TO " + role,
                          "GRANT USAGE,SELECT ON SEQUENCE audit.psim_outbox_sequence_seq TO " + role}) {
    result = co_await execute(connection, sql);
    if (!result) {
      co_return result;
    }
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::initialize() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto permissions = co_await (*lease)->query(
      {.sql = "SELECT NOT r.rolsuper AND NOT r.rolcreaterole AND NOT r.rolcreatedb "
              "AND NOT has_table_privilege(current_user,'audit.entries','UPDATE,DELETE,TRUNCATE') "
              "AND NOT has_table_privilege(current_user,'audit.checkpoints','UPDATE,DELETE,TRUNCATE') "
              "AND NOT has_schema_privilege(current_user,'audit','CREATE') "
              "AND has_table_privilege(current_user,'audit.entries','SELECT,INSERT') "
              "AND has_table_privilege(current_user,'audit.checkpoints','SELECT,INSERT') "
              "FROM pg_roles r WHERE r.rolname=current_user"});
  if (!permissions) {
    co_return std::unexpected(permissions.error());
  }
  if (permissions->size() != 1 || permissions->value(0, 0) != "t") {
    co_return p::fail(Code::kCommonInvalidArgument, "restricted append-only database role required");
  }
  auto archive = co_await blocking_->run([this] { return archive_.tenants(); });
  if (!archive) {
    co_return std::unexpected(archive.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::append(p::pg::Transaction* tx, audit::v1::AuditEventRecord input) {
  const auto& tenant = input.envelope().tenant_id();
  auto locked = co_await execute(&tx->connection(), "SELECT pg_advisory_xact_lock(hashtextextended($1,17))",
                                 {Param::text(tenant)});
  if (!locked) {
    co_return locked;
  }
  const auto source =
      input.envelope().causation_id().empty() ? input.envelope().message_id() : input.envelope().causation_id();
  auto previous = co_await tx->connection().query(
      {.sql = "SELECT body::text FROM audit.entries WHERE tenant=$1::uuid AND source=$2",
       .parameters = {Param::text(tenant), Param::text(source)}});
  if (!previous) {
    co_return std::unexpected(previous.error());
  }
  if (previous->size() != 0) {
    const auto entry = parse<Entry>(Json::parse(previous->value(0, 0).value_or("")));
    if (canonical(entry.entry()) != canonical(input.audit_entry())
        || canonical(entry.occurred_at()) != canonical(input.envelope().occurred_at())) {
      co_return p::fail(Code::kProcessingPayloadInvalid, "audit source identity reused with changed content");
    }
    co_return p::Result<void>{};
  }
  auto head = co_await tx->connection().query(
      {.sql = "SELECT body::text FROM audit.entries WHERE tenant=$1::uuid ORDER BY seq DESC LIMIT 1",
       .parameters = {Param::text(tenant)}});
  if (!head) {
    co_return std::unexpected(head.error());
  }
  Entry value;
  value.set_tenant_id(tenant);
  value.set_source_event_id(source);
  value.set_audit_seq(1);
  value.set_prev_hash(std::string(32, '\0'));
  if (head->size() != 0) {
    const auto last = parse<Entry>(Json::parse(head->value(0, 0).value_or("")));
    value.set_audit_seq(last.audit_seq() + 1);
    value.set_prev_hash(last.hash());
  }
  *value.mutable_entry() = input.audit_entry();
  *value.mutable_occurred_at() = input.envelope().occurred_at();
  *value.mutable_recorded_at() = Time::GetCurrentTime();
  value.set_hash(entry_hash(value));
  co_return co_await execute(&tx->connection(), "INSERT INTO audit.entries VALUES($1::uuid,$2::bigint,$3,$4::jsonb)",
                             {Param::text(tenant), Param::text(std::to_string(value.audit_seq())), Param::text(source),
                              Param::text(json(value).dump())});
}

asio::awaitable<p::Result<void>> Repository::apply(audit::v1::AuditEventRecord input, std::string key) {
  auto known = input;
  known.DiscardUnknownFields();
  if (canonical(known) != canonical(input)) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "unsupported audit fields require a newer consumer");
  }
  const auto& origin = input.envelope();
  if (!p::stream::uuid(origin.tenant_id()) || key != origin.tenant_id() || !p::stream::uuid(origin.message_id())
      || !Time::IsTimestampValid(origin.occurred_at()) || !origin.has_occurred_at()
      || (!origin.causation_id().empty() && !p::stream::uuid(origin.causation_id()))) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid audit envelope");
  }
  if (input.has_audit_checkpoint()) {
    if (origin.actor().id() != "audit-service" || origin.actor().kind() != common::v1::ACTOR_KIND_SERVICE
        || input.audit_checkpoint().tenant_id() != origin.tenant_id() || !signer_.verify(input.audit_checkpoint())) {
      co_return p::fail(Code::kProcessingPayloadInvalid, "invalid audit checkpoint");
    }
    co_return p::Result<void>{};
  }
  const auto& entry = input.audit_entry();
  if (!input.has_audit_entry() || entry.actor().id().empty()
      || entry.actor().kind() == common::v1::ACTOR_KIND_UNSPECIFIED
      || !common::v1::ActorKind_IsValid(entry.actor().kind()) || entry.action().empty() || entry.action().size() > 256
      || entry.outcome() < 1 || entry.outcome() > 3 || input.ByteSizeLong() > 1048576
      || (!entry.site_id().empty() && !p::stream::uuid(entry.site_id()))) {
    co_return p::fail(Code::kProcessingPayloadInvalid, "invalid audit entry");
  }
  co_return co_await p::pg::in_transaction<void>(pool_, [&](p::pg::Transaction* tx) { return append(tx, input); });
}

asio::awaitable<p::Result<void>> Repository::record_access(p::security::Principal principal, std::string action,
                                                           bool denied, Json details) {
  auto value = event(principal.tenant, principal.subject, denied ? "access.denied" : action, denied, details);
  (*value.mutable_audit_entry()->mutable_details())["requested_action"] = action;
  co_return co_await p::pg::in_transaction<void>(
      pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        co_return co_await messages_.append(
            tx, {value.envelope().message_id(), encode_("psim.audit.v1", principal.tenant, value)});
      });
}

asio::awaitable<p::Result<Json>> Repository::inspect(p::pg::Transaction* tx, std::string tenant, Json body,
                                                     bool exporting) {
  auto external = co_await blocking_->run([this, tenant] { return archive_.read(tenant); });
  if (!external) {
    co_return std::unexpected(external.error());
  }
  auto saved = co_await tx->connection().query(
      {.sql = "SELECT seq,body::text FROM audit.checkpoints WHERE tenant=$1::uuid ORDER BY seq",
       .parameters = {Param::text(tenant)}});
  if (!saved) {
    co_return std::unexpected(saved.error());
  }
  std::map<std::uint64_t, Checkpoint> checkpoints;
  for (const auto& value : *external) {
    if (!signer_.verify(value) || value.tenant_id() != tenant) {
      co_return Json{{"intact", false},
                     {"firstBrokenSeq", std::to_string(value.audit_seq())},
                     {"detail", "invalid external checkpoint signature"}};
    }
    checkpoints.emplace(value.audit_seq(), value);
  }
  for (std::size_t i = 0; i < saved->size(); ++i) {
    const auto value = parse<Checkpoint>(Json::parse(saved->value(i, 1).value_or("")));
    if (std::to_string(value.audit_seq()) != saved->value(i, 0) || value.tenant_id() != tenant
        || !signer_.verify(value)) {
      co_return Json{{"intact", false},
                     {"firstBrokenSeq", std::to_string(value.audit_seq())},
                     {"detail", "invalid stored checkpoint"}};
    }
    const auto found = checkpoints.find(value.audit_seq());
    if (found != checkpoints.end() && canonical(found->second) != canonical(value)) {
      co_return Json{{"intact", false},
                     {"firstBrokenSeq", std::to_string(value.audit_seq())},
                     {"detail", "checkpoint copies disagree"}};
    }
    checkpoints.emplace(value.audit_seq(), value);
  }
  const auto from = number(body, "fromSeq");
  const auto to = number(body, "toSeq");
  if (to != 0 && from > to) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid audit range");
  }
  Json result{{"intact", true},
              {"recordsChecked", "0"},
              {"firstBrokenSeq", "0"},
              {"detail", "chain and external checkpoints agree"}};
  std::uint64_t count = 0;
  std::uint64_t checked = 0;
  std::string previous(32, '\0');
  std::string bundle;
  if (exporting) {
    bundle =
        Json{{"format", "psim.audit.export.v1"}, {"tenantId", tenant}, {"query", body.value("query", Json::object())}}
            .dump()
        + '\n';
  }
  auto broken = [&result](std::uint64_t sequence, std::string detail) {
    result["intact"] = false;
    result["firstBrokenSeq"] = std::to_string(sequence);
    result["detail"] = std::move(detail);
  };
  for (;;) {
    auto rows =
        co_await tx->connection().query({.sql = "SELECT seq,source,CASE WHEN bytes<=8388608 THEN body ELSE NULL END "
                                                "FROM (SELECT seq,source,body::text,"
                                                "sum(octet_length(body::text)) OVER(ORDER BY seq) AS bytes FROM "
                                                "(SELECT seq,source,body FROM audit.entries WHERE tenant=$1::uuid AND "
                                                "seq>$2::bigint ORDER BY seq LIMIT 64) candidates) sized "
                                                "ORDER BY seq",
                                         .parameters = {Param::text(tenant), Param::text(std::to_string(count))}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    if (rows->size() == 0) {
      break;
    }
    for (std::size_t i = 0; i < rows->size(); ++i) {
      const auto encoded = rows->value(i, 2);
      if (!encoded) {
        if (i == 0) {
          broken(count + 1, "stored record exceeds read budget");
        }
        break;
      }
      Entry value;
      try {
        value = parse<Entry>(Json::parse(*encoded));
      } catch (const std::exception&) {
        broken(count + 1, "invalid stored record encoding");
        break;
      }
      const auto sequence = count + 1;
      if (value.tenant_id() != tenant || value.audit_seq() != sequence || std::to_string(sequence) != rows->value(i, 0)
          || value.source_event_id() != rows->value(i, 1) || value.prev_hash() != previous
          || entry_hash(value) != value.hash()) {
        broken(sequence, "record sequence or hash mismatch");
        break;
      }
      if (const auto point = checkpoints.find(sequence);
          point != checkpoints.end() && point->second.hash() != value.hash()) {
        broken(sequence, "chain disagrees with external checkpoint");
        break;
      }
      previous = value.hash();
      ++count;
      if ((from == 0 || count >= from) && (to == 0 || count <= to)) {
        ++checked;
      }
      if (exporting) {
        bundle +=
            Json{{"record", json(value)}, {"selected", matches(value, body.value("query", Json::object()))}}.dump()
            + '\n';
        if (bundle.size() > static_cast<std::size_t>(64 * 1024 * 1024)) {
          co_return p::fail(Code::kCommonInvalidArgument, "audit export exceeds 64 MiB snapshot limit");
        }
      }
    }
    if (!result["intact"].get<bool>()) {
      break;
    }
  }
  result["recordsChecked"] = std::to_string(checked);
  if (result["intact"].get<bool>()) {
    if (!checkpoints.empty() && checkpoints.rbegin()->first > count) {
      broken(count + 1, "records missing below external checkpoint");
    } else if ((from > count && from != 0) || (to > count && to != 0)) {
      co_return p::fail(Code::kCommonInvalidArgument, "range exceeds chain head");
    }
  }
  if (!checkpoints.empty()) {
    result["lastCheckpoint"] = json(checkpoints.rbegin()->second);
  }
  if (exporting) {
    if (!result["intact"].get<bool>()) {
      co_return p::fail(Code::kAuditIntegrityViolation, "audit integrity violation prevents export");
    }
    if (count != 0 && !checkpoints.contains(count)) {
      Checkpoint point;
      point.set_tenant_id(tenant);
      point.set_audit_seq(count);
      point.set_hash(previous);
      *point.mutable_created_at() = Time::GetCurrentTime();
      point = signer_.sign(std::move(point));
      auto anchored = co_await checkpoint(tx, point);
      if (!anchored) {
        co_return std::unexpected(anchored.error());
      }
      checkpoints.emplace(count, std::move(point));
    }
    for (const auto& [sequence, point] : checkpoints) {
      (void)sequence;
      bundle += Json{{"checkpoint", json(point)}}.dump() + '\n';
    }
    bundle += Json{{"headSeq", std::to_string(count)}, {"headHash", hex(previous)}}.dump() + '\n';
    if (bundle.size() > std::size_t{64} * 1024 * 1024) {
      co_return p::fail(Code::kCommonInvalidArgument, "audit export exceeds 64 MiB snapshot limit");
    }
    co_return Json{{"jsonl", std::move(bundle)}};
  }
  co_return result;
}

asio::awaitable<p::Result<void>> Repository::checkpoint(p::pg::Transaction* tx, Checkpoint point) {
  const auto& tenant = point.tenant_id();
  auto exists = co_await tx->connection().query(
      {.sql = "SELECT 1 FROM audit.checkpoints WHERE tenant=$1::uuid AND seq=$2::bigint",
       .parameters = {Param::text(tenant), Param::text(std::to_string(point.audit_seq()))}});
  if (!exists) {
    co_return std::unexpected(exists.error());
  }
  auto archived = co_await blocking_->run([this, point] { archive_.append(point); });
  if (!archived) {
    co_return std::unexpected(archived.error());
  }
  if (exists->size() != 0) {
    co_return p::Result<void>{};
  }
  auto inserted = co_await execute(
      &tx->connection(), "INSERT INTO audit.checkpoints VALUES($1::uuid,$2::bigint,$3::jsonb) ON CONFLICT DO NOTHING",
      {Param::text(tenant), Param::text(std::to_string(point.audit_seq())), Param::text(json(point).dump())});
  if (!inserted) {
    co_return inserted;
  }
  auto output = event(tenant, "audit-service", "audit.checkpoint", false, Json::object());
  output.mutable_envelope()->set_message_id(
      p::stream::uuid5(tenant, {"audit-checkpoint", std::to_string(point.audit_seq())}));
  output.clear_audit_entry();
  *output.mutable_audit_checkpoint() = point;
  co_return co_await messages_.append(tx, {output.envelope().message_id(), encode_("psim.audit.v1", tenant, output)});
}

asio::awaitable<p::Result<void>> Repository::tick() {
  auto external_tenants = co_await blocking_->run([this] { return archive_.tenants(); });
  if (!external_tenants) {
    co_return std::unexpected(external_tenants.error());
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows = co_await (*lease)->query({.sql = "SELECT DISTINCT tenant::text FROM audit.entries UNION SELECT DISTINCT "
                                               "tenant::text FROM audit.checkpoints"});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  std::set<std::string> tenants(external_tenants->begin(), external_tenants->end());
  for (std::size_t i = 0; i < rows->size(); ++i) {
    tenants.insert(std::string(rows->value(i, 0).value_or("")));
  }
  std::optional<p::Error> failure;
  for (const auto& tenant : tenants) {
    auto result = co_await p::pg::in_transaction<void>(
        pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          auto locked = co_await execute(&tx->connection(), "SELECT pg_advisory_xact_lock(hashtextextended($1,17))",
                                         {Param::text(tenant)});
          if (!locked) {
            co_return locked;
          }
          const auto now_ms = Time::TimestampToMilliseconds(Time::GetCurrentTime());
          auto heads = co_await tx->connection().query(
              {.sql = "SELECT (SELECT COALESCE(max(seq),0) FROM audit.entries WHERE tenant=$1::uuid),(SELECT "
                      "COALESCE(max(seq),0) FROM audit.checkpoints WHERE tenant=$1::uuid)",
               .parameters = {Param::text(tenant)}});
          if (!heads) {
            co_return std::unexpected(heads.error());
          }
          const auto head_seq = *heads->integer(0, 0);
          const auto checkpoint_seq = *heads->integer(0, 1);
          if (now_ms < next_checks_[tenant] && head_seq >= checkpoint_seq
              && head_seq - checkpoint_seq < checkpoint_records_) {
            co_return p::Result<void>{};
          }
          auto integrity = co_await inspect(tx, tenant, Json::object(), false);
          if (!integrity) {
            co_return std::unexpected(integrity.error());
          }
          if (!integrity->at("intact").get<bool>()) {
            co_return p::fail(Code::kAuditIntegrityViolation, "audit integrity violation");
          }
          next_checks_[tenant] = now_ms + interval_ms_;
          auto external = co_await blocking_->run([this, tenant] { return archive_.read(tenant); });
          if (!external) {
            co_return std::unexpected(external.error());
          }
          for (const auto& point : *external) {
            auto recovered = co_await checkpoint(tx, point);
            if (!recovered) {
              co_return recovered;
            }
          }
          auto saved = co_await tx->connection().query(
              {.sql = "SELECT body::text FROM audit.checkpoints WHERE tenant=$1::uuid ORDER BY seq",
               .parameters = {Param::text(tenant)}});
          if (!saved) {
            co_return std::unexpected(saved.error());
          }
          for (std::size_t i = 0; i < saved->size(); ++i) {
            const auto point = parse<Checkpoint>(Json::parse(saved->value(i, 0).value_or("")));
            auto archived = co_await blocking_->run([this, point] { archive_.append(point); });
            if (!archived) {
              co_return std::unexpected(archived.error());
            }
          }
          auto head = co_await tx->connection().query(
              {.sql = "SELECT body::text FROM audit.entries WHERE tenant=$1::uuid ORDER BY seq DESC LIMIT 1",
               .parameters = {Param::text(tenant)}});
          if (!head) {
            co_return std::unexpected(head.error());
          }
          if (head->size() == 0) {
            co_return p::Result<void>{};
          }
          const auto last = parse<Entry>(Json::parse(head->value(0, 0).value_or("")));
          const auto now = Time::GetCurrentTime();
          std::uint64_t last_seq = 0;
          std::int64_t last_ms = 0;
          if (integrity->contains("lastCheckpoint")) {
            const auto point = parse<Checkpoint>(integrity->at("lastCheckpoint"));
            last_seq = point.audit_seq();
            last_ms = Time::TimestampToMilliseconds(point.created_at());
          }
          if (last.audit_seq() == last_seq
              || (last.audit_seq() - last_seq < static_cast<std::uint64_t>(checkpoint_records_)
                  && Time::TimestampToMilliseconds(now) - last_ms < interval_ms_)) {
            co_return p::Result<void>{};
          }
          Checkpoint point;
          point.set_tenant_id(tenant);
          point.set_audit_seq(last.audit_seq());
          point.set_hash(last.hash());
          *point.mutable_created_at() = now;
          point = signer_.sign(std::move(point));
          co_return co_await checkpoint(tx, std::move(point));
        });
    if (!result) {
      failure = result.error();
      next_checks_[tenant] = 0;
    }
  }
  if (failure) {
    co_return std::unexpected(*failure);
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<Json>> Repository::request(p::security::Principal principal, std::string operation,
                                                     Json body) {
  const auto action = "audit." + operation;
  const auto query = body.value("query", Json::object());
  const auto site = query.value("siteId", std::string{});
  if (!p::stream::uuid(principal.tenant) || (!site.empty() && !p::stream::uuid(site))) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid tenant or site");
  }
  auto permission =
      p::authz::decide(principal, action, {principal.tenant, operation == "search" ? site : std::string{}},
                       std::chrono::system_clock::now());
  if (!permission) {
    auto logged = co_await record_access(principal, action, true);
    if (!logged) {
      co_return std::unexpected(logged.error());
    }
    co_return std::unexpected(permission.error());
  }
  auto logged = co_await record_access(principal, action, false);
  if (!logged) {
    co_return std::unexpected(logged.error());
  }
  auto result = co_await p::pg::in_transaction<
      Json>(pool_, [&, this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
    if (operation == "verify" || operation == "export") {
      auto locked = co_await execute(&tx->connection(), "SELECT pg_advisory_xact_lock(hashtextextended($1,17))",
                                     {Param::text(principal.tenant)});
      if (!locked) {
        co_return std::unexpected(locked.error());
      }
      co_return co_await inspect(tx, principal.tenant, body, operation == "export");
    }
    const auto size = body.value("pageSize", 100);
    if (size < 0 || size > 1000) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid page size");
    }
    const auto limit = size == 0 ? 100 : size;
    std::uint64_t before = 9223372036854775807ULL;
    if (body.contains("pageToken")) {
      const auto token = Json::parse(unhex(body.at("pageToken").get<std::string>()));
      if (token.at("tenant") != principal.tenant || token.at("query") != query) {
        co_return p::fail(Code::kCommonInvalidArgument, "cursor query mismatch");
      }
      before = number(token, "before");
    }
    auto time_parameter = [&query](const char* name) {
      return query.contains(name) ? Param::text(query.at(name).get<std::string>()) : Param::null();
    };
    auto rows = co_await tx->connection().query(
        {.sql = "WITH candidates AS (SELECT seq,body::text FROM audit.entries WHERE tenant=$1::uuid AND seq<$2::bigint "
                "AND ($4='' OR body->'entry'->'actor'->>'id'=$4) "
                "AND ($5='' OR body->'entry'->>'objectType'=$5) "
                "AND ($6='' OR body->'entry'->>'objectId'=$6) "
                "AND ($7='' OR body->'entry'->>'siteId'=$7) "
                "AND ($8='' OR body->'entry'->>'outcome'=$8) "
                "AND starts_with(body->'entry'->>'action',$9) "
                "AND ($10::timestamptz IS NULL OR (body->>'occurredAt')::timestamptz >= $10::timestamptz) "
                "AND ($11::timestamptz IS NULL OR (body->>'occurredAt')::timestamptz <= $11::timestamptz) "
                "ORDER BY seq DESC LIMIT LEAST($3::integer,65)), sized AS (SELECT seq,body,"
                "sum(octet_length(body)) OVER(ORDER BY seq DESC) AS bytes FROM candidates) "
                "SELECT CASE WHEN bytes<=8388608 THEN body ELSE NULL END FROM sized ORDER BY seq DESC",
         .parameters = {
             Param::text(principal.tenant), Param::text(std::to_string(before)), Param::text(std::to_string(limit + 1)),
             Param::text(query.value("actorId", std::string{})), Param::text(query.value("objectType", std::string{})),
             Param::text(query.value("objectId", std::string{})), Param::text(query.value("siteId", std::string{})),
             Param::text(query.value("outcome", std::string{})), Param::text(query.value("action", std::string{})),
             time_parameter("from"), time_parameter("to")}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    Json result{{"records", Json::array()}};
    for (std::size_t i = 0; i < std::min(rows->size(), static_cast<std::size_t>(std::min(limit, 64))); ++i) {
      const auto value = rows->value(i, 0);
      if (!value) {
        if (i == 0) {
          co_return p::fail(Code::kAuditIntegrityViolation, "stored record exceeds read budget");
        }
        break;
      }
      result["records"].push_back(Json::parse(*value));
    }
    if (rows->size() > result["records"].size() && !result["records"].empty()) {
      result["nextPageToken"] =
          hex(Json{{"tenant", principal.tenant}, {"query", query}, {"before", result["records"].back().at("auditSeq")}}
                  .dump());
    }
    co_return result;
  });
  if (operation == "verify" && result) {
    auto logged_result = co_await record_access(
        principal, result->at("intact").get<bool>() ? "audit.integrity.verified" : "audit.integrity.violated", false,
        {{"first_broken_seq", result->value("firstBrokenSeq", std::string("0"))}});
    if (!logged_result) {
      co_return std::unexpected(logged_result.error());
    }
  }
  co_return result;
}
}  // namespace psim::audit_service

// NOLINTEND(cppcoreguidelines-avoid-capturing-lambda-coroutines)
