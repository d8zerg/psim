#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "model.hpp"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "repository.hpp"

namespace psim::resource_catalog {
namespace pg = p::pg;
using Param = pg::Parameter;
using Code = p::ErrorCode;

asio::awaitable<p::Result<Json>> Repository::start_import(Mutation mutation, std::vector<Json> rows, bool dry_run) {
  if (rows.size() > kMaxImportRows
      || !p::authz::decide_somewhere(mutation.principal, "catalog.write", std::chrono::system_clock::now())) {
    co_return p::fail(Code::kAuthForbidden, "import requires administrator role");
  }
  co_return co_await pg::in_transaction<Json>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [this, &mutation, &rows, dry_run](pg::Transaction* tx) -> asio::awaitable<p::Result<Json>> {
        auto locked = co_await lock(tx, mutation.principal.tenant + ":import:" + mutation.key);
        if (!locked) {
          co_return std::unexpected(locked.error());
        }
        auto replay = co_await previous(tx, mutation);
        if (!replay) {
          co_return std::unexpected(replay.error());
        }
        if (*replay) {
          co_return **replay;
        }
        const auto id = p::stream::uuid7();
        Json report{{"importId", id},           {"state", "IMPORT_STATE_RUNNING"},
                    {"rowsTotal", rows.size()}, {"rowsCreated", 0},
                    {"rowsUpdated", 0},         {"rowsFailed", 0},
                    {"errors", Json::array()},  {"startedAt", timestamp(std::chrono::system_clock::now())}};
        Json const principal{{"subject", mutation.principal.subject},
                             {"scopes", mutation.principal.scopes},
                             {"roles", mutation.principal.roles}};
        auto job = co_await tx->connection().query(
            {.sql = "INSERT INTO catalog.imports(tenant,id,principal,report,dry_run) "
                    "VALUES($1::uuid,$2::uuid,$3::jsonb,$4::jsonb,$5::boolean)",
             .parameters = {Param::text(mutation.principal.tenant), Param::text(id), Param::text(principal.dump()),
                            Param::text(report.dump()), Param::boolean(dry_run)}});
        if (!job) {
          co_return std::unexpected(job.error());
        }
        for (std::size_t begin = 0; begin < rows.size(); begin += 500) {
          Json batch = Json::array();
          for (auto index = begin; index < std::min(rows.size(), begin + 500); ++index) {
            batch.push_back({{"row", index + 1}, {"body", std::move(rows[index])}});
          }
          auto inserted = co_await tx->connection().query(
              {.sql =
                   "INSERT INTO catalog.import_rows SELECT $1::uuid,$2::uuid,x.row,COALESCE(x.body,'null'::jsonb) FROM "
                   "jsonb_to_recordset($3::jsonb) AS x(row integer,body jsonb)",
               .parameters = {Param::text(mutation.principal.tenant), Param::text(id), Param::text(batch.dump())}});
          if (!inserted) {
            co_return std::unexpected(inserted.error());
          }
        }
        auto saved = co_await remember(tx, mutation, report);
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        auto audited = co_await audit(tx, mutation, "import", id, "", std::chrono::system_clock::now());
        if (!audited) {
          co_return std::unexpected(audited.error());
        }
        co_return report;
      });
}

asio::awaitable<p::Result<Json>> Repository::get_import(p::security::Principal principal, std::string id) {
  if (!canonical_uuid(id)) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid import identity");
  }
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto rows = co_await (*lease)->query(
      {.sql = "SELECT report::text,principal->>'subject' FROM catalog.imports WHERE tenant=$1::uuid AND id=$2::uuid",
       .parameters = {Param::text(principal.tenant), Param::text(id)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  if (rows->size() == 0U) {
    co_return p::fail(Code::kCommonNotFound, "import not found");
  }
  if (!p::authz::decide_somewhere(principal, "catalog.write", std::chrono::system_clock::now())
      || (rows->value(0, 1) != principal.subject && !p::authz::all_scopes(principal))) {
    co_return p::fail(Code::kAuthForbidden, "import belongs to another administrator");
  }
  co_return Json::parse(rows->value(0, 0).value_or(""));
}

asio::awaitable<p::Result<bool>> Repository::import_once() {
  co_return co_await pg::in_transaction<bool>(
      pool_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): owned until awaited.
      [this](pg::Transaction* tx) -> asio::awaitable<p::Result<bool>> {
        auto jobs = co_await tx->connection().query(
            {.sql = "SELECT tenant::text,id::text,principal::text FROM catalog.imports "
                    "WHERE report->>'state'='IMPORT_STATE_RUNNING' ORDER BY id LIMIT 1"});
        if (!jobs) {
          co_return std::unexpected(jobs.error());
        }
        if (!jobs->size()) {
          co_return false;
        }
        const std::string tenant(jobs->value(0, 0).value_or(""));
        const std::string id(jobs->value(0, 1).value_or(""));
        auto locked = co_await lock(tx, tenant);
        if (!locked) {
          co_return std::unexpected(locked.error());
        }
        auto current = co_await tx->connection().query(
            {.sql =
                 "SELECT report::text,processed,dry_run FROM catalog.imports WHERE tenant=$1::uuid AND id=$2::uuid AND "
                 "report->>'state'='IMPORT_STATE_RUNNING' FOR UPDATE SKIP LOCKED",
             .parameters = {Param::text(tenant), Param::text(id)}});
        if (!current) {
          co_return std::unexpected(current.error());
        }
        if (!current->size()) {
          co_return false;
        }
        auto report = Json::parse(current->value(0, 0).value_or(""));
        const bool dry_run = current->value(0, 2) == "t";
        auto processed = *current->integer(0, 1);
        const auto actor = Json::parse(jobs->value(0, 2).value_or(""));
        Mutation mutation{.principal = {.subject = actor.at("subject"),
                                        .tenant = tenant,
                                        .roles = actor.at("roles").get<std::set<std::string>>(),
                                        .scopes = actor.at("scopes").get<std::set<std::string>>(),
                                        .expires_at = Time::max()},
                          .action = "import"};
        auto rows = co_await tx->connection().query(
            {.sql = "SELECT row,body::text FROM catalog.import_rows WHERE tenant=$1::uuid AND job=$2::uuid AND "
                    "row>$3::integer ORDER BY row LIMIT 64",
             .parameters = {Param::text(tenant), Param::text(id), Param::integer(processed)}});
        if (!rows) {
          co_return std::unexpected(rows.error());
        }
        for (std::size_t index = 0; index < rows->size(); ++index) {
          processed = *rows->integer(index, 0);
          auto body = Json::parse(rows->value(index, 1).value_or(""));
          auto resource = parse("device", body);
          p::Result<Json> result =
              resource ? p::Result<Json>{Json{}} : p::Result<Json>{std::unexpected(resource.error())};
          bool updated = false;
          if (resource) {
            auto seen = co_await tx->connection().query(
                {.sql =
                     "INSERT INTO catalog.import_seen VALUES($1::uuid,$2::uuid,$3,$4) ON CONFLICT DO NOTHING RETURNING "
                     "external",
                 .parameters = {Param::text(tenant), Param::text(id), Param::text(resource->connector),
                                Param::text(resource->external)}});
            if (!seen) {
              co_return std::unexpected(seen.error());
            }
            if (!seen->size()) {
              result = p::fail(Code::kCatalogDuplicateExternalId, "duplicate external identity within import");
            } else {
              auto existing = co_await tx->connection().query(
                  {.sql = "SELECT id::text,version FROM catalog.resources WHERE tenant=$1::uuid AND kind='device' AND "
                          "connector=$2 AND external=$3",
                   .parameters = {Param::text(tenant), Param::text(resource->connector),
                                  Param::text(resource->external)}});
              if (!existing) {
                co_return std::unexpected(existing.error());
              }
              mutation.expected = 0;
              if (existing->size()) {
                if (body.contains("deviceId") && body["deviceId"] != existing->value(0, 0).value_or("")) {
                  result = p::fail(Code::kCatalogDuplicateExternalId, "device id conflicts with external identity");
                } else {
                  updated = true;
                  resource->id = std::string(existing->value(0, 0).value_or(""));
                  resource->body["deviceId"] = resource->id;
                  mutation.expected = static_cast<std::uint64_t>(*existing->integer(0, 1));
                }
              }
              if (result) {
                auto savepoint = co_await tx->connection().query({.sql = "SAVEPOINT import_row"});
                if (!savepoint) {
                  co_return std::unexpected(savepoint.error());
                }
                result = co_await apply(tx, std::move(*resource), mutation, std::chrono::system_clock::now(), !dry_run);
                if (!result || dry_run) {
                  auto rollback = co_await tx->connection().query({.sql = "ROLLBACK TO SAVEPOINT import_row"});
                  if (!rollback) {
                    co_return std::unexpected(rollback.error());
                  }
                }
                auto released = co_await tx->connection().query({.sql = "RELEASE SAVEPOINT import_row"});
                if (!released) {
                  co_return std::unexpected(released.error());
                }
              }
            }
          }
          if (result) {
            const auto* const counter = updated ? "rowsUpdated" : "rowsCreated";
            report[counter] = report.at(counter).get<unsigned int>() + 1;
          } else {
            report["rowsFailed"] = report.at("rowsFailed").get<unsigned int>() + 1;
            if (report["errors"].size() < 1000) {
              report["errors"].push_back(
                  {{"row", processed}, {"code", result.error().name()}, {"message", result.error().message()}});
            }
          }
        }
        if (processed >= report.at("rowsTotal").get<std::int64_t>()) {
          report["state"] = "IMPORT_STATE_COMPLETED";
          report["finishedAt"] = timestamp(std::chrono::system_clock::now());
          mutation.action = dry_run ? "validate" : "complete";
          auto audited = co_await audit(tx, mutation, "import", id, "", std::chrono::system_clock::now());
          if (!audited) {
            co_return std::unexpected(audited.error());
          }
          for (const auto* sql : {"DELETE FROM catalog.import_rows WHERE tenant=$1::uuid AND job=$2::uuid",
                                  "DELETE FROM catalog.import_seen WHERE tenant=$1::uuid AND job=$2::uuid"}) {
            auto removed =
                co_await tx->connection().query({.sql = sql, .parameters = {Param::text(tenant), Param::text(id)}});
            if (!removed) {
              co_return std::unexpected(removed.error());
            }
          }
        }
        auto saved =
            co_await tx->connection().query({.sql = "UPDATE catalog.imports SET report=$3::jsonb,processed=$4::integer "
                                                    "WHERE tenant=$1::uuid AND id=$2::uuid",
                                             .parameters = {Param::text(tenant), Param::text(id),
                                                            Param::text(report.dump()), Param::integer(processed)}});
        if (!saved) {
          co_return std::unexpected(saved.error());
        }
        co_return true;
      });
}
}  // namespace psim::resource_catalog
