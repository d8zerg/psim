#include "repository.hpp"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>

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

#include "psim/api/v1/rules.pb.h"
#include "psim/audit/v1/audit.pb.h"
#include "psim/common/v1/envelope.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/rules.pb.h"
#include "rules.hpp"

namespace psim::correlation::management {
namespace pg = p::pg;
using Param = pg::Parameter;
using Code = p::ErrorCode;
using Time = google::protobuf::util::TimeUtil;

namespace {
Json json(const google::protobuf::Message& message) {
  return Json::parse(p::stream::json(message));
}

template <typename T>
p::Result<T> parse(const Json& body) {
  T value;
  if (!google::protobuf::util::JsonStringToMessage(body.dump(), &value).ok()) {
    return p::fail(Code::kCommonInvalidArgument, "invalid request or unknown field");
  }
  return value;
}

void envelope(common::v1::Envelope* value, p::security::Principal principal, std::uint64_t version = 1) {
  value->set_message_id(p::stream::uuid7());
  value->set_tenant_id(principal.tenant);
  value->set_correlation_id(value->message_id());
  value->set_sequence(version);
  value->set_schema_version("1");
  *value->mutable_occurred_at() = Time::GetCurrentTime();
  *value->mutable_produced_at() = value->occurred_at();
  value->mutable_actor()->set_kind(common::v1::ACTOR_KIND_USER);
  value->mutable_actor()->set_id(principal.subject);
}

asio::awaitable<p::Result<rules::Rule>> load(pg::Transaction* tx, std::string tenant, std::string id,
                                             std::uint32_t version = 0) {
  if (!p::stream::uuid(id)) {
    co_return p::fail(Code::kCommonInvalidArgument, "rule UUID required");
  }
  auto rows = co_await tx->connection().query(
      {.sql = "SELECT body::text,state FROM rules.versions WHERE tenant=$1::uuid AND id=$2::uuid "
              "AND ($3::bigint=0 OR version=$3::bigint) ORDER BY version DESC LIMIT 1",
       .parameters = {Param::text(tenant), Param::text(id), Param::integer(version)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0) {
    co_return p::fail(Code::kCommonNotFound, "rule version not found");
  }
  auto result = parse<rules::Rule>(Json::parse(rows->value(0, 0).value_or("")));
  if (!result) {
    co_return std::unexpected(result.error());
  }
  auto state = rows->integer(0, 1);
  if (!state) {
    co_return std::unexpected(state.error());
  }
  result->set_state(static_cast<processing::v1::RuleState>(*state));
  co_return result;
}

asio::awaitable<p::Result<processing::v1::RuleSet>> active(pg::Transaction* tx, std::string tenant) {
  auto rows = co_await tx->connection().query(
      {.sql = "SELECT body::text FROM rules.active_sets WHERE tenant=$1::uuid", .parameters = {Param::text(tenant)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() != 0) {
    co_return parse<processing::v1::RuleSet>(Json::parse(rows->value(0, 0).value_or("")));
  }
  processing::v1::RuleSet result;
  result.set_rule_set_id(p::stream::uuid5("c8b07c6e-a8aa-5eb0-9744-705b51f8c211", {tenant, "ruleset"}));
  co_return result;
}
}  // namespace

bool can_read_rule(const rules::Rule& rule, const p::security::Principal& principal) {
  if (p::authz::all_scopes(principal)) {
    return true;
  }
  const bool sites = !rule.filter().site_ids().empty()
                     && std::ranges::all_of(rule.filter().site_ids(),
                                            [&](const auto& site) { return can_read_site(principal, site); });
  const bool zones =
      !rule.filter().zone_ids().empty() && std::ranges::all_of(rule.filter().zone_ids(), [&](const auto& zone) {
        return p::authz::scoped(principal, {principal.tenant, {}, zone});
      });
  return sites || zones;
}

bool can_read(const p::security::Principal& principal) {
  return p::stream::uuid(principal.tenant) && p::stream::uuid(principal.subject)
         && p::authz::decide_role(principal, "rule.read", std::chrono::system_clock::now()).has_value();
}

bool can_write(const p::security::Principal& principal) {
  return can_read(principal)
         && p::authz::decide(principal, "rule.write", {principal.tenant}, std::chrono::system_clock::now()).has_value();
}

bool can_read_site(const p::security::Principal& principal, std::string_view site) {
  return p::authz::scoped(principal, {principal.tenant, site});
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
  co_return co_await pg::Migrator("rules").apply(
      &**lease,
      {{.version = 1,
        .name = "immutable rules and private dry runs",
        .expand =
            {("CREATE TABLE rules.versions (tenant uuid NOT NULL,id uuid NOT NULL,version bigint NOT NULL "
              "CHECK(version>0),"
              "state integer NOT NULL CHECK(state BETWEEN 1 AND 4),body jsonb NOT NULL,PRIMARY "
              "KEY(tenant,id,version))"),
             "CREATE TABLE rules.active_sets (tenant uuid PRIMARY KEY,version bigint NOT NULL,body jsonb NOT NULL)",
             ("CREATE TABLE rules.requests (tenant uuid NOT NULL,actor uuid NOT NULL,key uuid NOT NULL,fingerprint "
              "text "
              "NOT NULL,"
              "response jsonb NOT NULL,PRIMARY KEY(tenant,actor,key))"),
             ("CREATE TABLE rules.dry_runs (tenant uuid NOT NULL,id uuid NOT NULL,owner uuid NOT NULL,principal jsonb "
              "NOT NULL,"
              "request jsonb NOT NULL,rule jsonb NOT NULL,body jsonb NOT NULL,input jsonb,result jsonb,"
              "finished boolean NOT NULL DEFAULT false,lease uuid,lease_until timestamptz,PRIMARY KEY(tenant,id))"),
             "CREATE INDEX rules_pending_runs ON rules.dry_runs(lease_until) WHERE NOT finished"}},
       {.version = 2,
        .name = "per-tenant pending replay limits",
        .expand = {"CREATE INDEX rules_pending_tenant ON rules.dry_runs(tenant) WHERE NOT finished"}}});
}

asio::awaitable<p::Result<Json>> Repository::request(p::security::Principal principal, std::string operation, Json body,
                                                     std::string key, std::optional<std::uint64_t> expected) {
  const bool mutation = operation == "save" || operation == "validate" || operation == "archive"
                        || operation == "activate" || operation == "dry-run";
  if (!can_read(principal) || (mutation && operation != "dry-run" && !can_write(principal))) {
    auto recorded = co_await denied(principal, operation);
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
    co_return p::fail(Code::kAuthForbidden, "rule operation is not allowed");
  }
  if (mutation && !p::stream::uuid(key)) {
    co_return p::fail(Code::kCommonInvalidArgument, "Idempotency-Key UUID required");
  }
  const auto fingerprint =
      p::stream::sha256(operation + ":" + body.dump() + ":" + (expected ? std::to_string(*expected) : ""));
  auto result = co_await pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited before captures are released
      [this, &principal, &operation, &body, &key, &fingerprint, mutation,
       expected](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        if (mutation) {
          auto locked = co_await tx->connection().query({.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1,0))",
                                                         .parameters = {Param::text("rules:" + principal.tenant)}});
          if (!locked) {
            co_return std::unexpected(locked.error());
          }
          auto previous = co_await tx->connection().query(
              {.sql = "SELECT fingerprint,response::text FROM rules.requests WHERE tenant=$1::uuid AND actor=$2::uuid "
                      "AND key=$3::uuid",
               .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key)}});
          if (!previous) {
            co_return std::unexpected(previous.error());
          }
          if (previous->size() != 0) {
            if (previous->value(0, 0) != fingerprint) {
              co_return p::fail(Code::kCommonIdempotencyKeyReused, "idempotency key has different content");
            }
            co_return Json::parse(previous->value(0, 1).value_or(""));
          }
        }
        auto response = co_await apply(tx, principal, operation, body, expected);
        if (!response) {
          co_return response;
        }
        if (mutation) {
          auto stored = co_await tx->connection().query(
              {.sql = "INSERT INTO rules.requests VALUES($1::uuid,$2::uuid,$3::uuid,$4,$5::jsonb)",
               .parameters = {Param::text(principal.tenant), Param::text(principal.subject), Param::text(key),
                              Param::text(fingerprint), Param::text(response->dump())}});
          if (!stored) {
            co_return std::unexpected(stored.error());
          }
        }
        co_return response;
      });
  if (!result && (result.error().code() == Code::kAuthForbidden || result.error().code() == Code::kAuthScopeDenied)) {
    auto recorded = co_await denied(principal, operation);
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
  }
  co_return result;
}

asio::awaitable<p::Result<void>> Repository::denied(p::security::Principal principal, std::string operation) {
  if (!p::stream::uuid(principal.tenant) || !p::stream::uuid(principal.subject)) {
    co_return p::Result<void>{};
  }
  co_return co_await pg::in_transaction<void>(pool_, [this, &principal, &operation](pg::Transaction* tx) {
    return audit(tx, principal, "access.denied", "", {{"action", "rule." + operation}});
  });
}

asio::awaitable<p::Result<void>> Repository::audit(pg::Transaction* tx, p::security::Principal principal,
                                                   std::string action, std::string id, Json details) {
  audit::v1::AuditEventRecord record;
  envelope(record.mutable_envelope(), principal);
  auto* entry = record.mutable_audit_entry();
  *entry->mutable_actor() = record.envelope().actor();
  entry->set_action(std::move(action));
  entry->set_object_type("correlation_rule");
  entry->set_object_id(std::move(id));
  entry->set_outcome(entry->action() == "access.denied" ? audit::v1::AUDIT_OUTCOME_DENIED
                                                        : audit::v1::AUDIT_OUTCOME_SUCCESS);
  (*entry->mutable_details())["snapshot"] = details.dump();
  co_return co_await messages_.append(
      tx, {record.envelope().message_id(), encode_("psim.audit.v1", principal.tenant, record)});
}

asio::awaitable<p::Result<void>> Repository::publish(pg::Transaction* tx, p::security::Principal principal,
                                                     processing::v1::RuleSet* output) {
  auto& set = *output;
  *set.mutable_activated_at() = Time::GetCurrentTime();
  set.mutable_activated_by()->set_kind(common::v1::ACTOR_KIND_USER);
  set.mutable_activated_by()->set_id(principal.subject);
  config::v1::ConfigRecord record;
  envelope(record.mutable_envelope(), principal, set.version());
  *record.mutable_rule_set() = set;
  if (record.ByteSizeLong() > 1048576) {
    co_return p::fail(Code::kProcessingRuleInvalid, "active rule set exceeds 1 MiB");
  }
  auto changed = co_await tx->connection().query(
      {.sql = "INSERT INTO rules.active_sets VALUES($1::uuid,$2::bigint,$3::jsonb) ON CONFLICT(tenant) "
              "DO UPDATE SET version=EXCLUDED.version,body=EXCLUDED.body",
       .parameters = {Param::text(principal.tenant), Param::integer(static_cast<std::int64_t>(set.version())),
                      Param::text(json(set).dump())}});
  if (!changed) {
    co_return std::unexpected(changed.error());
  }
  co_return co_await messages_.append(
      tx, {record.envelope().message_id(), encode_("psim.config.v1", "ruleset/" + principal.tenant, record)});
}

asio::awaitable<p::Result<Json>> Repository::apply(pg::Transaction* tx, p::security::Principal principal,
                                                   std::string operation, Json body,
                                                   std::optional<std::uint64_t> expected) {
  if ((operation == "validate" || operation == "archive" || operation == "activate") && !expected) {
    co_return p::fail(Code::kCommonPreconditionRequired, "If-Match version required");
  }
  if (operation == "get") {
    auto request = parse<api::v1::GetRuleRequest>(body);
    if (!request) {
      co_return std::unexpected(request.error());
    }
    auto rule = co_await load(tx, principal.tenant, request->rule_id(), request->version());

    if (!rule) {
      co_return std::unexpected(rule.error());
    }
    if (!can_read_rule(*rule, principal)) {
      co_return p::fail(Code::kAuthScopeDenied, "rule outside access scope");
    }
    co_return Json{{"rule", json(*rule)}};
  }
  if (operation == "list") {
    auto request = parse<api::v1::ListRulesRequest>(body);
    if (!request || request->page_size() < 0 || request->page_size() > 200
        || (!request->page_token().empty() && !p::stream::uuid(request->page_token()))) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid rule list parameters");
    }
    Json states = Json::array();
    for (const auto state : request->states()) {
      if (!processing::v1::RuleState_IsValid(state) || state == 0) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid rule state filter");
      }
      states.push_back(state);
    }
    const auto size = request->page_size() == 0 ? 100 : request->page_size();
    auto rows = co_await tx->connection().query(
        {.sql = "SELECT body::text,state,id::text FROM (SELECT DISTINCT ON(id) id,body,state FROM rules.versions "
                "WHERE tenant=$1::uuid AND id::text>$2 ORDER BY id,version DESC) latest "
                "WHERE ($4::boolean OR (jsonb_array_length(body->'filter'->'siteIds')>0 AND NOT EXISTS("
                "SELECT 1 FROM jsonb_array_elements_text(body->'filter'->'siteIds') AS sites(value) WHERE NOT "
                "($5::jsonb ? value))) OR (jsonb_array_length(body->'filter'->'zoneIds')>0 AND NOT EXISTS("
                "SELECT 1 FROM jsonb_array_elements_text(body->'filter'->'zoneIds') AS zones(value) WHERE NOT "
                "($7::jsonb ? value)))) "
                "AND (jsonb_array_length($6::jsonb)=0 OR $6::jsonb @> jsonb_build_array(state)) ORDER BY id LIMIT "
                "$3::integer",
         .parameters = {Param::text(principal.tenant), Param::text(request->page_token()), Param::integer(size + 1),
                        Param::boolean(p::authz::all_scopes(principal)),
                        Param::text(Json(p::authz::scope_filter(principal).sites).dump()), Param::text(states.dump()),
                        Param::text(Json(p::authz::scope_filter(principal).zones).dump())}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    Json values = Json::array();
    std::string after;
    for (std::size_t i = 0; i < rows->size() && std::cmp_less(i, size); ++i) {
      auto rule = parse<rules::Rule>(Json::parse(rows->value(i, 0).value_or("")));
      auto state = rows->integer(i, 1);
      if (!rule || !state) {
        co_return p::fail(Code::kCommonInternal, "invalid stored rule");
      }
      rule->set_state(static_cast<processing::v1::RuleState>(*state));
      if (can_read_rule(*rule, principal)
          && (request->states().empty() || std::ranges::contains(request->states(), rule->state()))) {
        values.push_back(json(*rule));
      }
      after = std::string(rows->value(i, 2).value_or(""));
    }
    co_return Json{{"rules", values}, {"nextPageToken", rows->size() > static_cast<std::size_t>(size) ? after : ""}};
  }
  if (operation == "active") {
    if (!parse<api::v1::GetActiveRuleSetRequest>(body)) {
      co_return p::fail(Code::kCommonInvalidArgument, "unexpected active-set query fields");
    }
    if (!p::authz::all_scopes(principal)) {
      co_return p::fail(Code::kAuthScopeDenied, "tenant rule set requires global scope");
    }
    auto set = co_await active(tx, principal.tenant);
    if (!set) {
      co_return std::unexpected(set.error());
    }
    co_return Json{{"ruleSet", json(*set)}};
  }
  if (operation == "save") {
    auto parsed = rules::parse_document(body.dump());
    if (!parsed) {
      co_return std::unexpected(parsed.error());
    }
    auto rule = std::move(*parsed);
    if (rule.rule_id().empty()) {
      rule.set_rule_id(p::stream::uuid7());
    }
    if (!p::stream::uuid(rule.rule_id())) {
      co_return p::fail(Code::kCommonInvalidArgument, "rule UUID required");
    }
    auto rows = co_await tx->connection().query(
        {.sql = "SELECT COALESCE(max(version),0)+1 FROM rules.versions WHERE tenant=$1::uuid AND id=$2::uuid",
         .parameters = {Param::text(principal.tenant), Param::text(rule.rule_id())}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    auto version = rows->integer(0, 0);
    if (!version || std::cmp_greater(*version, std::numeric_limits<std::uint32_t>::max())) {
      co_return p::fail(Code::kCommonConcurrentModification, "rule version exhausted");
    }
    if (*version > 1 && !expected) {
      co_return p::fail(Code::kCommonPreconditionRequired, "If-Match version required for a new draft");
    }
    if (expected && std::cmp_not_equal(*expected, *version - 1)) {
      co_return p::fail(Code::kCommonConcurrentModification, "rule version changed");
    }
    rule.set_version(static_cast<std::uint32_t>(*version));
    rule.set_state(processing::v1::RULE_STATE_DRAFT);
    *rule.mutable_created_at() = Time::GetCurrentTime();
    rule.clear_created_by();
    rule.mutable_created_by()->set_kind(common::v1::ACTOR_KIND_USER);
    rule.mutable_created_by()->set_id(principal.subject);
    auto stored = co_await tx->connection().query(
        {.sql = "INSERT INTO rules.versions VALUES($1::uuid,$2::uuid,$3::bigint,1,$4::jsonb)",
         .parameters = {Param::text(principal.tenant), Param::text(rule.rule_id()), Param::integer(rule.version()),
                        Param::text(json(rule).dump())}});
    if (!stored) {
      co_return std::unexpected(stored.error());
    }
    auto audited = co_await audit(tx, principal, "rule.draft_saved", rule.rule_id(), json(rule));
    if (!audited) {
      co_return std::unexpected(audited.error());
    }
    co_return Json{{"rule", json(rule)}};
  }
  if (operation == "validate") {
    auto request = parse<api::v1::ValidateRuleRequest>(body);
    if (!request || request->version() == 0) {
      co_return p::fail(Code::kCommonInvalidArgument, "positive rule version required");
    }
    auto rule = co_await load(tx, principal.tenant, request->rule_id(), request->version());
    if (operation == "validate" && expected != request->version()) {
      co_return p::fail(Code::kCommonConcurrentModification, "rule version changed");
    }
    if (!rule) {
      co_return std::unexpected(rule.error());
    }
    if (rule->state() != processing::v1::RULE_STATE_DRAFT && rule->state() != processing::v1::RULE_STATE_TESTING) {
      co_return p::fail(Code::kProcessingRuleStateInvalid, "only draft or testing versions can be validated");
    }
    auto checked = rules::compile(*rule, taxonomy_);
    Json violations = Json::array();
    for (const auto& error : checked.violations) {
      violations.push_back({{"field", error.field}, {"code", "PROCESSING_RULE_INVALID"}, {"message", error.message}});
    }
    if (checked.compiled) {
      auto changed = co_await tx->connection().query(
          {.sql = "UPDATE rules.versions SET state=2 WHERE tenant=$1::uuid AND id=$2::uuid AND version=$3::bigint",
           .parameters = {Param::text(principal.tenant), Param::text(rule->rule_id()),
                          Param::integer(rule->version())}});
      if (!changed) {
        co_return std::unexpected(changed.error());
      }
      rule->set_state(processing::v1::RULE_STATE_TESTING);
    }
    auto audited =
        co_await audit(tx, principal, "rule.validated", rule->rule_id(),
                       {{"valid", checked.compiled.has_value()}, {"violations", violations}, {"rule", json(*rule)}});
    if (!audited) {
      co_return std::unexpected(audited.error());
    }
    co_return Json{{"valid", checked.compiled.has_value()}, {"violations", violations}, {"rule", json(*rule)}};
  }
  if (operation == "activate" || operation == "archive") {
    auto previous = co_await active(tx, principal.tenant);
    if (!previous) {
      co_return std::unexpected(previous.error());
    }
    processing::v1::RuleSet next = *previous;
    std::string archived;
    if (operation == "activate") {
      auto request = parse<api::v1::ActivateRuleSetRequest>(body);
      if (!request || request->rules_size() > 1000) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid rule set");
      }
      if (expected != previous->version()) {
        co_return p::fail(Code::kCommonConcurrentModification, "active rule set version changed");
      }
      if (request->expected_rule_set_version() != previous->version()) {
        co_return p::fail(Code::kProcessingRulesetConflict, "active rule set version changed");
      }
      next.clear_rules();
      std::set<std::string> ids;
      for (const auto& reference : request->rules()) {
        if (reference.version() == 0 || !ids.insert(reference.rule_id()).second) {
          co_return p::fail(Code::kProcessingRuleInvalid, "duplicate or invalid rule reference");
        }
        auto rule = co_await load(tx, principal.tenant, reference.rule_id(), reference.version());
        if (!rule) {
          co_return std::unexpected(rule.error());
        }
        if (rule->state() != processing::v1::RULE_STATE_TESTING && rule->state() != processing::v1::RULE_STATE_ACTIVE) {
          co_return p::fail(Code::kProcessingRuleStateInvalid, "only testing or active versions can be activated");
        }
        if (!rules::compile(*rule, taxonomy_).compiled) {
          co_return p::fail(Code::kProcessingRuleInvalid, "rule no longer validates");
        }
        rule->set_state(processing::v1::RULE_STATE_ACTIVE);
        *next.add_rules() = *rule;
      }
    } else {
      auto request = parse<api::v1::ArchiveRuleRequest>(body);
      if (!request) {
        co_return std::unexpected(request.error());
      }
      auto latest = co_await load(tx, principal.tenant, request->rule_id());
      if (!latest) {
        co_return std::unexpected(latest.error());
      }
      if (expected != latest->version()) {
        co_return p::fail(Code::kCommonConcurrentModification, "rule version changed");
      }
      archived = request->rule_id();
      next.clear_rules();
      for (const auto& rule : previous->rules()) {
        if (rule.rule_id() != archived) {
          *next.add_rules() = rule;
        }
      }
      auto updated = co_await tx->connection().query(
          {.sql = "UPDATE rules.versions SET state=4 WHERE tenant=$1::uuid AND id=$2::uuid",
           .parameters = {Param::text(principal.tenant), Param::text(archived)}});
      if (!updated) {
        co_return std::unexpected(updated.error());
      }
    }
    if (operation == "activate" || next.rules_size() != previous->rules_size()) {
      if (previous->version() >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        co_return p::fail(Code::kCommonConcurrentModification, "rule set version exhausted");
      }
      next.set_version(previous->version() + 1);
      auto retired =
          co_await tx->connection().query({.sql = "UPDATE rules.versions SET state=4 WHERE tenant=$1::uuid AND state=3",
                                           .parameters = {Param::text(principal.tenant)}});
      if (!retired) {
        co_return std::unexpected(retired.error());
      }
      for (const auto& rule : next.rules()) {
        auto changed = co_await tx->connection().query(
            {.sql = "UPDATE rules.versions SET state=3 WHERE tenant=$1::uuid AND id=$2::uuid AND version=$3::bigint",
             .parameters = {Param::text(principal.tenant), Param::text(rule.rule_id()),
                            Param::integer(rule.version())}});
        if (!changed) {
          co_return std::unexpected(changed.error());
        }
      }
      auto published = co_await publish(tx, principal, &next);
      if (!published) {
        co_return std::unexpected(published.error());
      }
    }
    Json removed = Json::array();
    for (const auto& old : previous->rules()) {
      if (old.output().priority() <= common::v1::PRIORITY_P2
          && !std::ranges::any_of(next.rules(), [&](const auto& rule) { return rule.rule_id() == old.rule_id(); })) {
        removed.push_back(old.rule_id());
      }
    }
    auto audited = co_await audit(
        tx, principal, operation == "activate" ? "ruleset.activated" : "rule.archived",
        archived.empty() ? next.rule_set_id() : archived,
        {{"ruleSet", json(next)}, {"criticalRulesDisabled", removed}, {"notifySupervisors", !removed.empty()}});
    if (!audited) {
      co_return std::unexpected(audited.error());
    }
    if (operation == "archive") {
      auto latest = co_await load(tx, principal.tenant, archived);
      if (!latest) {
        co_return std::unexpected(latest.error());
      }
      co_return Json{{"rule", json(*latest)}};
    }
    auto stored = co_await active(tx, principal.tenant);
    if (!stored) {
      co_return std::unexpected(stored.error());
    }
    co_return Json{{"ruleSet", json(*stored)}};
  }
  if (operation == "dry-run" || operation == "get-run") {
    co_return co_await jobs(tx, principal, operation, body);
  }
  co_return p::fail(Code::kCommonNotFound, "rule operation not found");
}
}  // namespace psim::correlation::management
