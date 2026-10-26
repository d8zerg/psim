#pragma once

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/pg/connection.hpp"

namespace psim::platform::pg {
enum class MigrationPhase : std::uint8_t { kExpand, kContract };

struct Migration {
  std::int64_t version = 0;
  std::string name;
  std::vector<std::string> expand{};
  std::vector<std::string> contract{};
  /// Oldest migration plan allowed to coexist with this expand phase; zero means version.
  std::int64_t minimum_reader = 0;
};

/// Transactional migration runner, serialized with an advisory transaction lock per schema.
/// SHA-256 detects editing applied migrations. Contract is a separate, explicitly authorized
/// operation of the release process after old service versions have been drained (ADR-024).
class Migrator {
 public:
  explicit Migrator(std::string schema) : schema_(std::move(schema)) {}

  asio::awaitable<Result<void>> apply(Connection* connection, std::vector<Migration> migrations,
                                      MigrationPhase phase = MigrationPhase::kExpand);

 private:
  std::string schema_;
};
}  // namespace psim::platform::pg
