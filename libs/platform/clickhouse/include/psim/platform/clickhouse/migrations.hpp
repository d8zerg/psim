#pragma once
#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/pg/pool.hpp"

namespace psim::platform::clickhouse {
enum class MigrationPhase : std::uint8_t { kExpand, kContract };

struct Migration {
  std::int64_t version = 0;
  std::string name;
  /// Trusted, idempotent DDL, including explicit ON CLUSTER where needed. A crash can replay
  /// the last statement. Replicated table paths/macros are identical in all topologies.
  std::vector<std::string> expand{};
  std::vector<std::string> contract{};
};

class Migrator {
 public:
  /// Target uniquely identifies the destination cluster/database in the control journal.
  Migrator(Pool* destination, pg::Pool* journal, std::string target);
  asio::awaitable<Result<void>> apply(std::vector<Migration> migrations,
                                      MigrationPhase phase = MigrationPhase::kExpand);

 private:
  Pool* destination_;
  pg::Pool* journal_;
  std::string target_;
};
}  // namespace psim::platform::clickhouse
