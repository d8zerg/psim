#include <expected>
#include <string>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"
#include "sql.hpp"

namespace psim::platform::pg {
asio::awaitable<Result<void>> ensure_schema(Connection* connection, std::string schema) {
  if (!connection || !detail::identifier(schema)) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL service schema");
  }
  const auto existing = co_await connection->query(
      {.sql = "SELECT 1 FROM pg_namespace WHERE nspname=$1", .parameters = {Parameter::text(schema)}});
  if (!existing) {
    co_return std::unexpected(existing.error());
  }
  if (existing->size() != 0) {
    co_return Result<void>{};
  }
  const auto created =
      co_await connection->query({.sql = *detail::schema_sql(schema, "CREATE SCHEMA IF NOT EXISTS @schema@")});
  if (!created) {
    co_return std::unexpected(created.error());
  }
  co_return Result<void>{};
}
}  // namespace psim::platform::pg
