#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "console_store.hpp"
#include "console_transaction.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/transaction.hpp"

namespace psim::projection {
namespace p = platform;
namespace asio = boost::asio;
using Param = p::pg::Parameter;

asio::awaitable<p::Result<std::string>> ConsoleStore::active() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto result = co_await (*lease)->query({.sql = "SELECT generation FROM console.active"});
  if (!result || result->size() != 1) {
    co_return p::fail(p::ErrorCode::kCommonUnavailable, "console generation unavailable");
  }
  co_return std::string(result->value(0, 0).value_or(""));
}

asio::awaitable<p::Result<void>> ConsoleStore::activate(bool compare) {
  co_return co_await console_transaction<void>(
      pool_, writer_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [this, compare](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        if (compare) {
          auto different = co_await tx->connection().query(
              {.sql = "WITH old AS (SELECT tenant,kind,id,version,confirmed,session,body FROM console.resources WHERE "
                      "replica=(SELECT generation FROM console.active)), "
                      "fresh AS (SELECT tenant,kind,id,version,confirmed,session,body FROM console.resources WHERE "
                      "replica=$1), "
                      "delta AS ((SELECT * FROM old EXCEPT SELECT * FROM fresh) UNION ALL (SELECT * FROM fresh EXCEPT "
                      "SELECT * FROM old)) "
                      "SELECT count(*) FROM delta WHERE EXISTS(SELECT 1 FROM old)",
               .parameters = {Param::text(replica_)}});
          if (!different) {
            co_return std::unexpected(different.error());
          }
          if (*different->integer(0, 0) != 0) {
            co_return p::fail(p::ErrorCode::kCommonConcurrentModification,
                              "retained Kafka history differs from the active console model");
          }
        }
        auto changed = co_await tx->connection().query(
            {.sql = "UPDATE console.active SET generation=$1", .parameters = {Param::text(replica_)}});
        if (!changed) {
          co_return std::unexpected(changed.error());
        }
        auto captured = co_await tx->connection().query(
            {.sql = "INSERT INTO console.history(tenant,kind,id,captured,confirmed,body) SELECT "
                    "tenant,kind,id,clock_timestamp(),confirmed,body FROM console.resources WHERE replica=$1",
             .parameters = {Param::text(replica_)}});
        if (!captured) {
          co_return std::unexpected(captured.error());
        }
        co_return p::Result<void>{};
      });
}

asio::awaitable<p::Result<void>> ConsoleStore::discard() {
  co_return co_await console_transaction<void>(
      pool_, writer_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [this](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        for (const auto* sql :
             {"DELETE FROM console.resources WHERE replica=$1 AND $1<>(SELECT generation FROM console.active)",
              "DELETE FROM console.positions WHERE replica=$1 AND $1<>(SELECT generation FROM console.active)",
              "DELETE FROM console.generations WHERE id=$1 AND $1<>(SELECT generation FROM console.active)"}) {
          auto removed = co_await tx->connection().query({.sql = sql, .parameters = {Param::text(replica_)}});
          if (!removed) {
            co_return std::unexpected(removed.error());
          }
        }
        co_return p::Result<void>{};
      });
}

asio::awaitable<p::Result<p::kafka::Offsets>> ConsoleStore::positions() {
  auto lease = co_await pool_->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto created =
      co_await (*lease)->query({.sql = "INSERT INTO console.generations(id) VALUES($1) ON CONFLICT DO NOTHING",
                                .parameters = {Param::text(replica_)}});
  if (!created) {
    co_return std::unexpected(created.error());
  }
  auto rows =
      co_await (*lease)->query({.sql = "SELECT topic,partition,next_offset FROM console.positions WHERE replica=$1",
                                .parameters = {Param::text(replica_)}});
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  p::kafka::Offsets result;
  for (std::size_t i = 0; i < rows->size(); ++i) {
    result[{std::string(rows->value(i, 0).value_or("")), static_cast<std::int32_t>(*rows->integer(i, 1))}] =
        *rows->integer(i, 2);
  }
  co_return result;
}

asio::awaitable<p::Result<void>> ConsoleStore::advance(p::kafka::Offsets positions) {
  co_return co_await console_transaction<void>(
      pool_, writer_,
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
      [this, &positions](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
        bool changed = false;
        for (const auto& [partition, offset] : positions) {
          auto stored = co_await tx->connection().query(
              {.sql =
                   "INSERT INTO console.positions VALUES($1,$2,$3,$4) ON CONFLICT(replica,topic,partition) DO UPDATE "
                   "SET next_offset=EXCLUDED.next_offset WHERE console.positions.next_offset<EXCLUDED.next_offset",
               .parameters = {Param::text(replica_), Param::text(partition.topic), Param::integer(partition.partition),
                              Param::integer(offset)}});
          if (!stored) {
            co_return std::unexpected(stored.error());
          }
          changed = changed || stored->affected() != 0;
        }
        if (changed) {
          auto revision =
              co_await tx->connection().query({.sql = "UPDATE console.generations SET revision=revision+1 WHERE id=$1",
                                               .parameters = {Param::text(replica_)}});
          if (!revision) {
            co_return std::unexpected(revision.error());
          }
        }
        co_return p::Result<void>{};
      });
}
}  // namespace psim::projection
