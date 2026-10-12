#pragma once

#include <boost/asio/awaitable.hpp>

#include <expected>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/transaction.hpp"

namespace psim::platform::pg {
/// Repository unit of work: the callback changes domain rows, inbox and outbox through the same
/// Transaction. Its result is returned only after COMMIT; errors roll back, with no implicit
/// retries. This is an infrastructure template; domain repository ports do not depend on pg.
template <typename T, typename Operation>
asio::awaitable<Result<T>> in_transaction(Pool* pool, Operation operation,
                                          Isolation isolation = Isolation::kReadCommitted) {
  if (pool == nullptr) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL unit of work requires a pool");
  }
  auto lease = co_await pool->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  auto transaction = co_await Transaction::begin(&**lease, isolation);
  if (!transaction) {
    co_return std::unexpected(transaction.error());
  }
  auto result = co_await operation(&*transaction);
  if (!result) {
    const auto rollback = co_await transaction->rollback();
    (void)rollback;  // preserve the original error; failed rollback discards the connection
    co_return std::unexpected(result.error());
  }
  const auto committed = co_await transaction->commit();
  if (!committed) {
    co_return std::unexpected(committed.error());
  }
  co_return std::move(result);
}
}  // namespace psim::platform::pg
