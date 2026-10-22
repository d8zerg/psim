#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "psim/platform/pg/transaction.hpp"

namespace psim::platform::timers {
struct Timer {
  std::string tenant, owner, id;
  std::int64_t due_ms = 0;
};

// The owner lock is acquired before firing or cancelling a timer. Firing, domain changes and
// outbox append must use the same transaction. Polling never establishes ownership (ADR-012).
class Store {
 public:
  explicit Store(std::string schema);
  [[nodiscard]] std::vector<std::string> migration() const;
  boost::asio::awaitable<Result<void>> schedule(pg::Transaction* tx, Timer timer) const;
  boost::asio::awaitable<Result<std::vector<Timer>>> due(pg::Connection* connection) const;
  boost::asio::awaitable<Result<bool>> fire(pg::Transaction* tx, Timer timer) const;
  boost::asio::awaitable<Result<void>> cancel(pg::Transaction* tx, std::string tenant, std::string owner) const;

 private:
  [[nodiscard]] std::string sql(std::string statement) const;
  std::string table_;
};
}  // namespace psim::platform::timers
