#include "psim/platform/pg/migrations.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "sql.hpp"

namespace psim::platform::pg {
asio::awaitable<Result<void>> Migrator::apply(Connection* connection, std::vector<Migration> migrations,
                                              MigrationPhase phase) {
  if (!connection || !detail::identifier(schema_) || migrations.empty()) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL migration plan");
  }
  std::int64_t previous = 0;
  for (const auto& migration : migrations) {
    if (migration.version <= previous || migration.name.empty() || migration.expand.empty()
        || migration.minimum_reader < 0 || migration.minimum_reader > migration.version) {
      co_return fail(ErrorCode::kCommonInvalidArgument,
                     "migrations must have unique increasing versions and expand steps");
    }
    previous = migration.version;
  }
  auto transaction = co_await Transaction::begin(connection);
  if (!transaction) {
    co_return std::unexpected(transaction.error());
  }
  auto lock = co_await connection->query({.sql = "SELECT pg_advisory_xact_lock(hashtextextended($1, 0))",
                                          .parameters = {Parameter::text(schema_ + ":migrations")}});
  if (!lock) {
    co_return std::unexpected(lock.error());
  }
  auto schema = co_await ensure_schema(connection, schema_);
  if (!schema) {
    co_return schema;
  }
  constexpr auto* kCreateMigrations =
      "CREATE TABLE IF NOT EXISTS @schema@.psim_migrations (version bigint NOT NULL, phase text NOT NULL, name text "
      "NOT NULL, checksum text NOT NULL, PRIMARY KEY (version, phase))";
  for (const auto* schema_ddl :
       {kCreateMigrations,
        "ALTER TABLE @schema@.psim_migrations ADD COLUMN IF NOT EXISTS minimum_reader bigint NOT NULL DEFAULT 0"}) {
    auto statement = detail::schema_sql(schema_, schema_ddl);
    const auto result = co_await connection->query({.sql = *statement});
    if (!result) {
      co_return std::unexpected(result.error());
    }
  }
  auto existing =
      co_await connection->query({.sql = *detail::schema_sql(schema_,
                                                             "SELECT version, phase, name, checksum, minimum_reader "
                                                             "FROM @schema@.psim_migrations ORDER BY version, phase")});
  if (!existing) {
    co_return std::unexpected(existing.error());
  }
  // Unknown expansions need an explicit reader floor. Unknown contract phases forbid rollback.
  for (std::size_t row = 0; row < existing->size(); ++row) {
    const auto version = existing->integer(row, 0);
    if (!version) {
      co_return std::unexpected(version.error());
    }
    const auto found = std::ranges::find(migrations, *version, &Migration::version);
    if (found == migrations.end()) {
      const auto floor = existing->integer(row, 4);
      if (*version > migrations.back().version && existing->value(row, 1) == "expand" && floor && *floor > 0
          && *floor <= migrations.back().version) {
        continue;
      }
      co_return fail(ErrorCode::kCommonConcurrentModification, "database migration history is newer or incomplete");
    }
    const auto applied_phase = existing->value(row, 1);
    if (applied_phase != "expand" && applied_phase != "contract") {
      co_return fail(ErrorCode::kCommonConcurrentModification, "unknown PostgreSQL migration phase");
    }
    const auto& statements = applied_phase == "expand" ? found->expand : found->contract;
    std::string content;
    for (const auto& migration_sql : statements) {
      content += std::to_string(migration_sql.size()) + ":" + migration_sql;
    }
    const auto floor = existing->integer(row, 4);
    if (!floor || *floor < 0
        || (*floor == 0 ? found->version : *floor)
               != (found->minimum_reader == 0 ? found->version : found->minimum_reader)
        || existing->value(row, 2) != found->name || existing->value(row, 3) != detail::fingerprint(content)) {
      co_return fail(ErrorCode::kCommonConcurrentModification, "applied PostgreSQL migration was modified");
    }
  }
  for (const auto& migration : migrations) {
    const std::string stage = phase == MigrationPhase::kExpand ? "expand" : "contract";
    bool applied = false;
    bool expanded = false;
    for (std::size_t row = 0; row < existing->size(); ++row) {
      if (existing->integer(row, 0) == Result<std::int64_t>(migration.version)) {
        applied = applied || existing->value(row, 1) == stage;
        expanded = expanded || existing->value(row, 1) == "expand";
      }
    }
    if (applied) {
      continue;
    }
    if (phase == MigrationPhase::kContract && !expanded) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "contract migration requires its expand phase first");
    }
    const auto& statements = phase == MigrationPhase::kExpand ? migration.expand : migration.contract;
    if (statements.empty()) {
      continue;
    }
    std::string content;
    for (const auto& migration_sql : statements) {
      auto statement = detail::schema_sql(schema_, migration_sql);
      if (!statement) {
        co_return std::unexpected(statement.error());
      }
      const auto result = co_await connection->query({.sql = *statement});
      if (!result) {
        co_return std::unexpected(result.error());
      }
      content += std::to_string(migration_sql.size()) + ":" + migration_sql;
    }
    const auto checksum = detail::fingerprint(content);
    if (checksum.empty()) {
      co_return fail(ErrorCode::kCommonInternal, "PostgreSQL migration checksum failed");
    }
    const auto recorded = co_await connection->query(
        {.sql = *detail::schema_sql(schema_,
                                    "INSERT INTO @schema@.psim_migrations (version, phase, name, checksum, "
                                    "minimum_reader) VALUES ($1, $2, $3, $4, $5)"),
         .parameters = {Parameter::integer(migration.version), Parameter::text(stage), Parameter::text(migration.name),
                        Parameter::text(checksum), Parameter::integer(migration.minimum_reader)}});
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
  }
  co_return co_await transaction->commit();
}
}  // namespace psim::platform::pg
