#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "psim/api/v1/rules.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "repository.hpp"
#include "rules.hpp"

namespace psim::correlation::management {
using Time = google::protobuf::util::TimeUtil;
using Code = p::ErrorCode;
using Param = p::pg::Parameter;

namespace {
template <typename T>
p::Result<T> parse(const Json& body) {
  T result;
  if (!google::protobuf::util::JsonStringToMessage(body.dump(), &result).ok()) {
    return p::fail(Code::kCommonInvalidArgument, "invalid dry-run request");
  }
  return result;
}

Json json(const google::protobuf::Message& value) {
  return Json::parse(p::stream::json(value));
}
}  // namespace

asio::awaitable<p::Result<Json>> Repository::jobs(p::pg::Transaction* tx, p::security::Principal principal,
                                                  std::string operation, Json body) {
  if (operation == "dry-run") {
    auto request = parse<api::v1::StartDryRunRequest>(body);
    if (!request || !p::stream::uuid(request->rule_id()) || request->version() == 0 || !request->has_from()
        || !request->has_to() || !Time::IsTimestampValid(request->from()) || !Time::IsTimestampValid(request->to())
        || request->from().nanos() % 1000000 != 0 || request->to().nanos() % 1000000 != 0
        || !api::v1::DryRunSource_IsValid(request->source())) {
      co_return p::fail(Code::kCommonInvalidArgument,
                        "valid rule, source and whole-millisecond historical range required");
    }
    const auto from = Time::TimestampToMilliseconds(request->from());
    const auto to = Time::TimestampToMilliseconds(request->to());
    if (from < 0 || from >= to || to - from > 86400000 || to > Time::TimestampToMilliseconds(Time::GetCurrentTime())) {
      co_return p::fail(Code::kProcessingDryRunRangeInvalid,
                        "dry-run interval must be historical and at most 24 hours");
    }
    auto rows = co_await tx->connection().query(
        {.sql =
             "SELECT body::text,state FROM rules.versions WHERE tenant=$1::uuid AND id=$2::uuid AND version=$3::bigint",
         .parameters = {Param::text(principal.tenant), Param::text(request->rule_id()),
                        Param::integer(request->version())}});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    if (rows->size() == 0) {
      co_return p::fail(Code::kCommonNotFound, "rule version not found");
    }
    const auto rule_json = Json::parse(rows->value(0, 0).value_or(""));
    auto rule = parse<rules::Rule>(rule_json);
    if (!rule || !rules::compile(*rule, taxonomy_).compiled) {
      co_return p::fail(Code::kProcessingRuleInvalid, "dry-run rule is invalid");
    }
    if (!principal.scopes.contains("*")
        && (rule->filter().site_ids().empty() || !std::ranges::all_of(rule->filter().site_ids(), [&](const auto& site) {
              return can_read_site(principal, site);
            }))) {
      co_return p::fail(Code::kAuthScopeDenied, "rule outside access scope");
    }
    auto pending = co_await tx->connection().query(
        {.sql = "SELECT count(*) FROM rules.dry_runs WHERE tenant=$1::uuid AND NOT finished",
         .parameters = {Param::text(principal.tenant)}});
    if (!pending) {
      co_return std::unexpected(pending.error());
    }
    auto pending_count = pending->integer(0, 0);
    if (!pending_count) {
      co_return std::unexpected(pending_count.error());
    }
    if (*pending_count >= 10) {
      co_return p::fail(Code::kCommonRateLimited, "tenant has too many pending dry runs");
    }
    api::v1::DryRun run;
    run.set_dry_run_id(p::stream::uuid7());
    run.set_rule_id(request->rule_id());
    run.set_version(request->version());
    run.set_state(api::v1::DRY_RUN_STATE_RUNNING);
    *run.mutable_started_at() = Time::GetCurrentTime();
    run.set_source(request->source() == api::v1::DRY_RUN_SOURCE_UNSPECIFIED ? api::v1::DRY_RUN_SOURCE_KAFKA
                                                                            : request->source());
    request->set_source(run.source());
    const Json identity{{"tenant", principal.tenant},
                        {"subject", principal.subject},
                        {"roles", principal.roles},
                        {"scopes", principal.scopes}};
    auto created = co_await tx->connection().query(
        {.sql = "INSERT INTO rules.dry_runs(tenant,id,owner,principal,request,rule,body) "
                "VALUES($1::uuid,$2::uuid,$3::uuid,$4::jsonb,$5::jsonb,$6::jsonb,$7::jsonb)",
         .parameters = {Param::text(principal.tenant), Param::text(run.dry_run_id()), Param::text(principal.subject),
                        Param::text(identity.dump()), Param::text(json(*request).dump()), Param::text(rule_json.dump()),
                        Param::text(json(run).dump())}});
    if (!created) {
      co_return std::unexpected(created.error());
    }
    auto audited =
        co_await audit(tx, principal, "rule.dry_run_started", run.dry_run_id(), {{"request", json(*request)}});
    if (!audited) {
      co_return std::unexpected(audited.error());
    }
    co_return Json{{"dryRun", json(run)}};
  }
  auto request = parse<api::v1::GetDryRunRequest>(body);
  if (!request || !p::stream::uuid(request->dry_run_id()) || request->page_size() < 0 || request->page_size() > 200) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid dry-run pagination");
  }
  std::size_t offset = 0;
  if (!request->page_token().empty()) {
    const auto& token = request->page_token();
    const auto converted = std::from_chars(token.data(), std::to_address(token.end()), offset);
    if (converted.ec != std::errc{} || converted.ptr != std::to_address(token.end()) || offset > 10000) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid dry-run cursor");
    }
  }
  auto rows = co_await tx->connection().query(
      {.sql = "SELECT owner::text,principal::text,body::text,result::text FROM rules.dry_runs WHERE tenant=$1::uuid "
              "AND id=$2::uuid",
       .parameters = {Param::text(principal.tenant), Param::text(request->dry_run_id())}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0) {
    co_return p::fail(Code::kCommonNotFound, "dry run not found");
  }
  if (rows->value(0, 0) != principal.subject && !can_write(principal)) {
    co_return p::fail(Code::kAuthScopeDenied, "dry run belongs to another user");
  }
  const auto identity = Json::parse(rows->value(0, 1).value_or(""));
  for (const auto& scope : identity.at("scopes")) {
    if (!can_read_site(principal, scope.get<std::string>())) {
      co_return p::fail(Code::kAuthScopeDenied, "dry-run scope no longer accessible");
    }
  }
  const auto run = Json::parse(rows->value(0, 2).value_or(""));
  const auto results = Json::parse(rows->value(0, 3).value_or("[]"));
  const auto size = request->page_size() == 0 ? 100 : request->page_size();
  const auto end = std::min(results.size(), offset + static_cast<std::size_t>(size));
  Json decisions = Json::array();
  Json signals = Json::array();
  for (auto i = offset; i < end; ++i) {
    decisions.push_back(results.at(i));
    if (!results.at(i).at("suppressed").get<bool>()) {
      signals.push_back(results.at(i).at("signal"));
    }
  }
  co_return Json{{"dryRun", run},
                 {"signals", signals},
                 {"decisions", decisions},
                 {"nextPageToken", end < results.size() ? std::to_string(end) : ""}};
}

asio::awaitable<p::Result<std::optional<Job>>> Repository::take_job() {
  co_return co_await p::pg::in_transaction<
      std::optional<Job>>(pool_, [](p::pg::Transaction* tx) -> asio::awaitable<p::Result<std::optional<Job>>> {
    auto rows = co_await tx->connection().query(
        {.sql = "SELECT tenant::text,id::text,principal::text,request::text,rule::text,input::text FROM rules.dry_runs "
                "WHERE NOT finished AND (lease_until IS NULL OR lease_until<clock_timestamp()) ORDER BY id LIMIT 1 FOR "
                "UPDATE SKIP LOCKED"});
    if (!rows) {
      co_return std::unexpected(rows.error());
    }
    if (rows->size() == 0) {
      co_return std::optional<Job>{};
    }
    Job job{.tenant = std::string(rows->value(0, 0).value_or("")),
            .id = std::string(rows->value(0, 1).value_or("")),
            .lease = p::stream::uuid7(),
            .principal = Json::parse(rows->value(0, 2).value_or("")),
            .request = Json::parse(rows->value(0, 3).value_or("")),
            .rule = Json::parse(rows->value(0, 4).value_or("")),
            .input = Json::parse(rows->value(0, 5).value_or("null"))};
    auto claimed = co_await tx->connection().query(
        {.sql = "UPDATE rules.dry_runs SET lease=$3::uuid,lease_until=clock_timestamp()+interval '15 seconds' WHERE "
                "tenant=$1::uuid AND id=$2::uuid",
         .parameters = {Param::text(job.tenant), Param::text(job.id), Param::text(job.lease)}});
    if (!claimed) {
      co_return std::unexpected(claimed.error());
    }
    co_return std::optional<Job>(std::move(job));
  });
}

asio::awaitable<p::Result<void>> Repository::renew(Job job) {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto updated =
      co_await (*lease)->query({.sql = "UPDATE rules.dry_runs SET lease_until=clock_timestamp()+interval '15 seconds' "
                                       "WHERE tenant=$1::uuid AND id=$2::uuid AND lease=$3::uuid AND NOT finished",
                                .parameters = {Param::text(job.tenant), Param::text(job.id), Param::text(job.lease)}});
  if (!updated) {
    co_return std::unexpected(updated.error());
  }
  if (updated->affected() != 1) {
    co_return p::fail(Code::kCommonConcurrentModification, "dry-run lease changed");
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::save_input(Job job, Json input) {
  const auto text = input.dump();
  if (text.size() > 8388608) {
    co_return p::fail(Code::kProcessingRuleInvalid, "dry-run input snapshot exceeds 8 MiB");
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto saved = co_await (*lease)->query(
      {.sql = "UPDATE rules.dry_runs SET input=$4::jsonb WHERE tenant=$1::uuid AND id=$2::uuid AND lease=$3::uuid AND "
              "NOT finished AND input IS NULL",
       .parameters = {Param::text(job.tenant), Param::text(job.id), Param::text(job.lease), Param::text(text)}});
  if (!saved) {
    co_return std::unexpected(saved.error());
  }
  if (saved->affected() != 1) {
    co_return p::fail(Code::kCommonConcurrentModification, "dry-run lease changed");
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Repository::finish(Job job, p::Result<rules::ReplayResult> result) {
  co_return co_await p::pg::in_transaction<void>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited before captures are released
      [this, &job, &result](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT body::text FROM rules.dry_runs WHERE tenant=$1::uuid AND id=$2::uuid AND lease=$3::uuid "
                    "AND NOT finished FOR UPDATE",
             .parameters = {Param::text(job.tenant), Param::text(job.id), Param::text(job.lease)}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        if (rows->size() == 0) {
          co_return p::fail(Code::kCommonConcurrentModification, "dry-run lease changed");
        }
        auto run = parse<api::v1::DryRun>(Json::parse(rows->value(0, 0).value_or("")));
        if (!run) {
          co_return std::unexpected(run.error());
        }
        Json decisions = Json::array();
        if (result) {
          for (const auto& decision : result->decisions) {
            Json versions = Json::array();
            for (const auto& [id, version] : decision.maintenance_versions) {
              versions.push_back({{"maintenanceId", id}, {"version", std::to_string(version)}});
            }
            decisions.push_back({{"signalId", decision.id},
                                 {"signal", json(decision.signal)},
                                 {"suppressed", decision.suppressed},
                                 {"maintenanceIds", decision.maintenance_ids},
                                 {"maintenanceVersions", versions}});
          }
          if (decisions.dump().size() > 16777216) {
            result = p::fail(Code::kProcessingRuleInvalid, "dry-run result exceeds 16 MiB");
            decisions = Json::array();
          }
        }
        run->set_state(result ? api::v1::DRY_RUN_STATE_COMPLETED : api::v1::DRY_RUN_STATE_FAILED);
        *run->mutable_finished_at() = Time::GetCurrentTime();
        if (result) {
          run->set_events_processed(result->processed);
          run->set_signals_produced(result->decisions.size());
        } else {
          run->set_failure(std::string(result.error().name()));
        }
        auto saved = co_await tx->connection().query(
            {.sql = "UPDATE rules.dry_runs SET body=$3::jsonb,result=$4::jsonb,finished=true,lease_until=NULL WHERE "
                    "tenant=$1::uuid AND id=$2::uuid",
             .parameters = {Param::text(job.tenant), Param::text(job.id), Param::text(json(*run).dump()),
                            Param::text(decisions.dump())}});
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        p::security::Principal principal;
        principal.tenant = job.tenant;
        principal.subject = job.principal.at("subject").get<std::string>();
        co_return co_await audit(tx, principal, result ? "rule.dry_run_completed" : "rule.dry_run_failed", job.id,
                                 json(*run));
      });
}
}  // namespace psim::correlation::management
