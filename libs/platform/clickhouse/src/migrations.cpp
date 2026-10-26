#include "psim/platform/clickhouse/migrations.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "detail.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/transaction.hpp"

namespace psim::platform::clickhouse {
Migrator::Migrator(Pool* destination, pg::Pool* journal, std::string target)
    : destination_(destination), journal_(journal), target_(std::move(target)) {}

asio::awaitable<Result<void>> Migrator::apply(std::vector<Migration> migrations, MigrationPhase phase) {
  if (destination_ == nullptr || journal_ == nullptr || target_.empty() || migrations.empty()) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse migration plan");
  }
  std::int64_t previous = 0;
  for (const auto& migration : migrations) {
    if (migration.version <= previous || migration.name.empty() || migration.expand.empty()
        || migration.minimum_reader < 0 || migration.minimum_reader > migration.version) {
      co_return fail(ErrorCode::kCommonInvalidArgument,
                     "ClickHouse migrations must have increasing versions and expand DDL");
    }
    previous = migration.version;
  }
  auto lease = co_await journal_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto bootstrap = co_await pg::Transaction::begin(&**lease);
  if (!bootstrap) {
    co_return std::unexpected(bootstrap.error());
  }
  auto locked_bootstrap = co_await (*lease)->query(
      {.sql = "SELECT pg_advisory_xact_lock(hashtextextended('psim-clickhouse-bootstrap', 0))"});
  if (!locked_bootstrap) {
    co_return std::unexpected(locked_bootstrap.error());
  }
  auto schema = co_await pg::ensure_schema(&**lease, "psim_clickhouse");
  if (!schema) {
    co_return schema;
  }
  for (const auto* ddl :
       {"CREATE TABLE IF NOT EXISTS psim_clickhouse.schema_migrations (target text NOT NULL, version bigint NOT NULL, "
        "phase text NOT NULL, name text NOT NULL, checksum text NOT NULL, step bigint NOT NULL DEFAULT 0, "
        "PRIMARY KEY (target, version, phase))",
        "DO $$ BEGIN IF NOT EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema='psim_clickhouse' "
        "AND table_name='schema_migrations' AND column_name='minimum_reader') THEN "
        "ALTER TABLE psim_clickhouse.schema_migrations ADD COLUMN minimum_reader bigint NOT NULL DEFAULT 0; END IF; "
        "END $$"}) {
    auto created = co_await (*lease)->query({.sql = ddl});
    if (!created) {
      co_return std::unexpected(created.error());
    }
  }
  auto bootstrap_committed = co_await bootstrap->commit();
  if (!bootstrap_committed) {
    co_return bootstrap_committed;
  }
  auto history = co_await (*lease)->query({.sql = "SELECT version, phase, name, checksum, minimum_reader FROM "
                                                  "psim_clickhouse.schema_migrations WHERE target=$1",
                                           .parameters = {pg::Parameter::text(target_)}});
  if (!history) {
    co_return std::unexpected(history.error());
  }
  for (std::size_t index = 0; index < history->size(); ++index) {
    const auto version = history->integer(index, 0);
    const auto floor = history->integer(index, 4);
    if (history->value(index, 1) != "expand" && history->value(index, 1) != "contract") {
      co_return fail(ErrorCode::kCommonConcurrentModification, "unknown ClickHouse migration phase");
    }
    if (version && *version > migrations.back().version && history->value(index, 1) == "expand" && floor && *floor > 0
        && *floor <= migrations.back().version) {
      continue;
    }
    bool found = false;
    for (const auto& migration : migrations) {
      if (version != Result<std::int64_t>(migration.version)) {
        continue;
      }
      std::string content;
      const auto& statements = history->value(index, 1) == "expand" ? migration.expand : migration.contract;
      for (const auto& statement : statements) {
        content += std::to_string(statement.size()) + ":" + statement;
      }
      found = floor && *floor >= 0
              && (*floor == 0 ? migration.version : *floor)
                     == (migration.minimum_reader == 0 ? migration.version : migration.minimum_reader)
              && history->value(index, 2) == migration.name && history->value(index, 3) == detail::fingerprint(content);
    }
    if (!found) {
      co_return fail(ErrorCode::kCommonConcurrentModification,
                     "ClickHouse migration history is newer, incomplete or modified");
    }
  }
  for (const auto& migration : migrations) {
    const auto& statements = phase == MigrationPhase::kExpand ? migration.expand : migration.contract;
    if (statements.empty()) {
      continue;
    }
    std::string content;
    for (const auto& statement : statements) {
      content += std::to_string(statement.size()) + ":" + statement;
    }
    const auto checksum = detail::fingerprint(content);
    const std::string stage = phase == MigrationPhase::kExpand ? "expand" : "contract";
    for (;;) {
      auto transaction = co_await pg::Transaction::begin(&**lease);
      if (!transaction) {
        co_return std::unexpected(transaction.error());
      }
      auto locked = co_await (*lease)->query({.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1, 0))",
                                              .parameters = {pg::Parameter::text("clickhouse-migrations/" + target_)}});
      if (!locked) {
        co_return std::unexpected(locked.error());
      }
      if (phase == MigrationPhase::kContract) {
        auto expanded = co_await (*lease)->query(
            {.sql =
                 "SELECT step FROM psim_clickhouse.schema_migrations WHERE target=$1 AND version=$2 AND phase='expand'",
             .parameters = {pg::Parameter::text(target_), pg::Parameter::integer(migration.version)}});
        if (!expanded) {
          co_return std::unexpected(expanded.error());
        }
        if (expanded->size() == 0
            || expanded->integer(0, 0) != Result<std::int64_t>(static_cast<std::int64_t>(migration.expand.size()))) {
          co_return fail(ErrorCode::kCommonPreconditionRequired,
                         "ClickHouse contract requires a completed expand phase");
        }
      }
      auto row = co_await (*lease)->query(
          {.sql = "SELECT checksum, step FROM psim_clickhouse.schema_migrations WHERE target=$1 AND version=$2 AND "
                  "phase=$3",
           .parameters = {pg::Parameter::text(target_), pg::Parameter::integer(migration.version),
                          pg::Parameter::text(stage)}});
      if (!row) {
        co_return std::unexpected(row.error());
      }
      if (row->size() == 0) {
        auto registered = co_await (*lease)->query(
            {.sql = "INSERT INTO psim_clickhouse.schema_migrations (target,version,phase,name,checksum,minimum_reader) "
                    "VALUES "
                    "($1,$2,$3,$4,$5,$6)",
             .parameters = {pg::Parameter::text(target_), pg::Parameter::integer(migration.version),
                            pg::Parameter::text(stage), pg::Parameter::text(migration.name),
                            pg::Parameter::text(checksum), pg::Parameter::integer(migration.minimum_reader)}});
        if (!registered) {
          co_return std::unexpected(registered.error());
        }
        auto committed = co_await transaction->commit();
        if (!committed) {
          co_return std::unexpected(committed.error());
        }
        continue;
      }
      const auto step = row->integer(0, 1);
      if (row->value(0, 0) != checksum || !step || *step < 0 || static_cast<std::size_t>(*step) > statements.size()) {
        co_return fail(ErrorCode::kCommonConcurrentModification, "ClickHouse migration checksum or step differs");
      }
      if (static_cast<std::size_t>(*step) == statements.size()) {
        auto committed = co_await transaction->commit();
        if (!committed) {
          co_return std::unexpected(committed.error());
        }
        break;
      }
      auto applied = co_await destination_->execute_ddl(statements[static_cast<std::size_t>(*step)]);
      if (!applied) {
        co_return std::unexpected(applied.error());
      }
      auto advanced = co_await (*lease)->query(
          {.sql =
               "UPDATE psim_clickhouse.schema_migrations SET step=step+1 WHERE target=$1 AND version=$2 AND phase=$3",
           .parameters = {pg::Parameter::text(target_), pg::Parameter::integer(migration.version),
                          pg::Parameter::text(stage)}});
      if (!advanced) {
        co_return std::unexpected(advanced.error());
      }
      auto committed = co_await transaction->commit();
      if (!committed) {
        co_return std::unexpected(committed.error());
      }
    }
  }
  co_return Result<void>{};
}
}  // namespace psim::platform::clickhouse
