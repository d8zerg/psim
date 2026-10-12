#include "psim/platform/pg/transaction.hpp"

#include <expected>
#include <string>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/pg/connection.hpp"

namespace psim::platform::pg {
Transaction::Transaction(Transaction&& other) noexcept : connection_(std::exchange(other.connection_, nullptr)) {}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
  if (this != &other) {
    if (connection_) {
      connection_->close();
    }
    connection_ = std::exchange(other.connection_, nullptr);
  }
  return *this;
}

Transaction::~Transaction() {
  if (connection_) {
    connection_->close();
  }
}

asio::awaitable<Result<Transaction>> Transaction::begin(Connection* connection, Isolation isolation) {
  if (!connection || !connection->reusable()) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL transaction needs an idle connection");
  }
  std::string sql;
  switch (isolation) {
    case Isolation::kReadCommitted:
      sql = "BEGIN ISOLATION LEVEL READ COMMITTED";
      break;
    case Isolation::kRepeatableRead:
      sql = "BEGIN ISOLATION LEVEL REPEATABLE READ";
      break;
    case Isolation::kSerializable:
      sql = "BEGIN ISOLATION LEVEL SERIALIZABLE";
      break;
  }
  const auto started = co_await connection->query({.sql = std::move(sql)});
  if (!started) {
    co_return std::unexpected(started.error());
  }
  co_return Transaction(connection);
}

asio::awaitable<Result<void>> Transaction::commit() {
  if (!connection_) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL transaction already finished");
  }
  const auto result = co_await connection_->query({.sql = "COMMIT"});
  if (result) {
    connection_ = nullptr;
    if (result->command() != "COMMIT") {
      co_return fail(ErrorCode::kCommonConcurrentModification, "PostgreSQL transaction was rolled back");
    }
  }
  co_return result ? Result<void>{} : Result<void>(std::unexpected(result.error()));
}

asio::awaitable<Result<void>> Transaction::rollback() {
  if (!connection_) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL transaction already finished");
  }
  const auto result = co_await connection_->query({.sql = "ROLLBACK"});
  if (result) {
    connection_ = nullptr;
  }
  co_return result ? Result<void>{} : Result<void>(std::unexpected(result.error()));
}
}  // namespace psim::platform::pg
