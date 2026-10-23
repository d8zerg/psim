#pragma once
#include <expected>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/transaction.hpp"

namespace psim::projection {
template <typename T, typename Operation>
boost::asio::awaitable<platform::Result<T>> console_transaction(platform::pg::Pool* pool,
                                                                platform::pg::Connection* connection,
                                                                Operation operation) {
  if (!connection) {
    co_return co_await platform::pg::in_transaction<T>(pool, std::move(operation));
  }
  auto transaction = co_await platform::pg::Transaction::begin(connection);
  if (!transaction) {
    co_return std::unexpected(transaction.error());
  }
  auto result = co_await operation(&*transaction);
  if (!result) {
    (void)co_await transaction->rollback();
    co_return std::unexpected(result.error());
  }
  auto committed = co_await transaction->commit();
  if (!committed) {
    co_return std::unexpected(committed.error());
  }
  co_return std::move(result);
}
}  // namespace psim::projection
