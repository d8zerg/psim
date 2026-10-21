#pragma once

#include <boost/asio/awaitable.hpp>
#include <clickhouse/block.h>
#include <clickhouse/query.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::clickhouse {
namespace asio = boost::asio;
using Duration = std::chrono::milliseconds;

class Metrics;

struct Endpoint {
  std::string host;
  std::uint16_t port = 9440;
};

struct PoolOptions {
  std::vector<Endpoint> endpoints{};
  std::string database = "default";
  std::string user = "psim";
  std::string password_file{};
  security::TlsOptions tls{};
  bool allow_plaintext = false;
  std::size_t size = 4;
  Duration acquire_timeout{5000};
  Duration connect_timeout{3000};
  Duration socket_timeout{5000};
  Duration query_timeout{30000};
  std::size_t max_result_rows = 100000;
  std::size_t max_result_bytes = std::size_t{16} * 1024 * 1024;
};

/// Only identifiers become SQL text. All values use native query parameters; the mandatory
/// scope cannot be overridden by predicates. Bounds are Unix milliseconds, [from_ms, to_ms).
struct Select {
  std::string table;
  std::vector<std::string> columns{};
  std::string tenant;
  std::string time_column = "occurred_at";
  std::int64_t from_ms = 0;
  std::int64_t to_ms = 0;
  std::map<std::string, std::string> equals{};
  std::size_t limit = 1000;
  bool final = true;
};

/// Fixed, reviewed SELECT text with native parameters. Scope values are reserved and supplied
/// separately; request data must never become statement text.
struct Read {
  std::string statement;
  std::string tenant;
  std::int64_t from_ms = 0;
  std::int64_t to_ms = 0;
  std::map<std::string, std::string> parameters{};
};

struct ResultSet {
  std::vector<::clickhouse::Block> blocks{};
  std::size_t rows = 0;
  std::size_t bytes = 0;
};

/// Shard-bound lazy connection pool. Native calls run in BlockingPool with exclusive leases.
/// Failed connections are discarded. Writes are never retried implicitly: their outcome may
/// be unknown. The next lease rotates to another replica. close() wakes pending borrowers.
class Pool {
 public:
  Pool(async::BlockingPool* blocking, PoolOptions options, Metrics* metrics = nullptr);
  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;
  Pool(Pool&&) = delete;
  Pool& operator=(Pool&&) = delete;
  ~Pool();

  asio::awaitable<Result<ResultSet>> select(Select query);
  asio::awaitable<Result<ResultSet>> read(Read query);
  asio::awaitable<Result<void>> insert(std::string table, ::clickhouse::Block block, std::string token);
  /// Trusted deployment/migration SQL only. Never pass request data to this administrative API.
  asio::awaitable<Result<void>> execute_ddl(std::string statement);
  void close();

 private:
  asio::awaitable<Result<ResultSet>> query_native(::clickhouse::Query query);
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace psim::platform::clickhouse
