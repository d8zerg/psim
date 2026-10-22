#include "psim/platform/timers/store.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/transaction.hpp"

namespace psim::platform::timers {
namespace asio = boost::asio;
using Param = pg::Parameter;

Store::Store(std::string schema) {
  if (schema.empty() || !std::ranges::all_of(schema, [](char ch) { return (ch >= 'a' && ch <= 'z') || ch == '_'; })) {
    throw std::invalid_argument("invalid timer schema");
  }
  table_ = std::move(schema) + ".timers";
}

std::string Store::sql(std::string statement) const {
  std::size_t offset = 0;
  while ((offset = statement.find("@timers@", offset)) != std::string::npos) {
    statement.replace(offset, 8, table_);
    offset += table_.size();
  }
  return statement;
}

std::vector<std::string> Store::migration() const {
  return {sql("CREATE TABLE @timers@(tenant uuid NOT NULL,owner uuid NOT NULL,id text NOT NULL,due_ms bigint NOT "
              "NULL,fired_at timestamptz,cancelled boolean NOT NULL DEFAULT false,PRIMARY KEY(tenant,id))"),
          sql("CREATE INDEX timers_due ON @timers@(due_ms) WHERE fired_at IS NULL AND NOT cancelled")};
}

asio::awaitable<Result<void>> Store::schedule(pg::Transaction* tx, Timer timer) const {
  auto rows = co_await tx->connection().query(
      {.sql = sql("INSERT INTO @timers@(tenant,owner,id,due_ms) VALUES($1::uuid,$2::uuid,$3,$4::bigint) ON CONFLICT "
                  "DO NOTHING"),
       .parameters = {Param::text(timer.tenant), Param::text(timer.owner), Param::text(timer.id),
                      Param::text(std::to_string(timer.due_ms))}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  co_return Result<void>{};
}

asio::awaitable<Result<std::vector<Timer>>> Store::due(pg::Connection* connection) const {
  auto rows =
      co_await connection->query({.sql = sql("SELECT tenant::text,owner::text,id,due_ms FROM @timers@ WHERE fired_at "
                                             "IS NULL AND NOT cancelled AND due_ms<=extract(epoch FROM "
                                             "clock_timestamp())*1000 ORDER BY due_ms LIMIT 256")});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  std::vector<Timer> output;
  output.reserve(rows->size());
  for (std::size_t index = 0; index < rows->size(); ++index) {
    output.push_back({std::string(rows->value(index, 0).value_or("")), std::string(rows->value(index, 1).value_or("")),
                      std::string(rows->value(index, 2).value_or("")),
                      std::stoll(std::string(rows->value(index, 3).value_or("")))});
  }
  co_return output;
}

asio::awaitable<Result<bool>> Store::fire(pg::Transaction* tx, Timer timer) const {
  auto rows = co_await tx->connection().query(
      {.sql = sql("UPDATE @timers@ SET fired_at=clock_timestamp() WHERE tenant=$1::uuid AND id=$2 AND "
                  "owner=$3::uuid AND fired_at IS NULL AND NOT cancelled AND "
                  "due_ms<=extract(epoch FROM clock_timestamp())*1000 RETURNING id"),
       .parameters = {Param::text(timer.tenant), Param::text(timer.id), Param::text(timer.owner)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  co_return rows->size() != 0;
}

asio::awaitable<Result<void>> Store::cancel(pg::Transaction* tx, std::string tenant, std::string owner) const {
  auto rows = co_await tx->connection().query(
      {.sql = sql("UPDATE @timers@ SET cancelled=true WHERE tenant=$1::uuid AND owner=$2::uuid AND fired_at IS NULL"),
       .parameters = {Param::text(tenant), Param::text(owner)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  co_return Result<void>{};
}
}  // namespace psim::platform::timers
