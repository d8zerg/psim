#pragma once

#include <boost/asio/awaitable.hpp>

#include <cstdint>

#include "psim/platform/error.hpp"
#include "psim/platform/pg/connection.hpp"

namespace psim::platform::pg {
enum class Isolation : std::uint8_t { kReadCommitted, kRepeatableRead, kSerializable };

/// RAII transaction on an exclusive connection. Explicit commit/rollback are asynchronous;
/// dropping an unfinished transaction closes its connection and rolls back on the server.
class Transaction {
 public:
  [[nodiscard]] static asio::awaitable<Result<Transaction>> begin(Connection* connection,
                                                                  Isolation isolation = Isolation::kReadCommitted);
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  Transaction(Transaction&& other) noexcept;
  Transaction& operator=(Transaction&& other) noexcept;
  ~Transaction();
  asio::awaitable<Result<void>> commit();
  asio::awaitable<Result<void>> rollback();

  [[nodiscard]] bool active() const noexcept { return connection_ != nullptr; }

  [[nodiscard]] Connection& connection() const noexcept { return *connection_; }

 private:
  explicit Transaction(Connection* connection) : connection_(connection) {}

  Connection* connection_;
};
}  // namespace psim::platform::pg
