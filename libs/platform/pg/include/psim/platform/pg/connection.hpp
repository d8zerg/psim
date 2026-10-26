#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/error.hpp"

struct pg_conn;    // NOLINT(readability-identifier-naming): libpq opaque C handle
struct pg_result;  // NOLINT(readability-identifier-naming): libpq opaque C handle

namespace psim::platform::pg {
namespace asio = boost::asio;
using Duration = std::chrono::milliseconds;

struct ConnectionOptions {
  std::string host = "127.0.0.1";
  std::string port = "5432";
  std::string database = "psim";
  std::string user = "psim";
  std::string password{};
  /// libpq settings, including sslrootcert/sslcert/sslkey and target_session_attrs.
  /// TLS verifies the host and CA by default; development may explicitly set sslmode=disable.
  std::map<std::string, std::string> properties{};
  Duration connect_timeout{5000};
  Duration query_timeout{30000};
  std::size_t max_result_bytes = std::size_t{16} * 1024 * 1024;
};

/// Owns a SQL parameter. A missing value is SQL NULL; binary values can contain zero bytes.
struct Parameter {
  std::optional<std::string> value{};
  std::uint32_t type = 0;
  bool binary = false;

  [[nodiscard]] static Parameter text(std::string value, std::uint32_t type = 0);
  [[nodiscard]] static Parameter integer(std::int64_t value);
  [[nodiscard]] static Parameter boolean(bool value);
  [[nodiscard]] static Parameter bytes(std::string value);
  [[nodiscard]] static Parameter null(std::uint32_t type = 0);
};

struct Query {
  std::string sql{};
  std::vector<Parameter> parameters{};
};

/// Owns a libpq result. Views live until this object is destroyed; SQL NULL has no view.
class Rows {
 public:
  explicit Rows(pg_result* result) noexcept : result_(result) {}

  Rows(const Rows&) = delete;
  Rows& operator=(const Rows&) = delete;
  Rows(Rows&& other) noexcept;
  Rows& operator=(Rows&& other) noexcept;
  ~Rows();

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t columns() const noexcept;
  [[nodiscard]] std::optional<std::string_view> value(std::size_t row, std::size_t column) const noexcept;
  [[nodiscard]] Result<std::int64_t> integer(std::size_t row, std::size_t column) const;
  [[nodiscard]] std::uint32_t column_type(std::size_t column) const noexcept;
  [[nodiscard]] std::string_view column_name(std::size_t column) const noexcept;
  [[nodiscard]] std::string_view command() const noexcept;
  [[nodiscard]] std::uint64_t affected() const noexcept;

 private:
  pg_result* result_;
};

struct Notification {
  std::string channel;
  std::string payload;
};

/// libpq on one Asio executor (ADR-014). One operation at a time per connection; the pool lends
/// exclusive connections. A timeout, cancellation or transport failure closes the connection:
/// an unfinished query or transaction can never leak to its next borrower. Failed writes and
/// COMMIT are never retried automatically because their outcome may be unknown.
class Connection {
 public:
  [[nodiscard]] static asio::awaitable<Result<std::unique_ptr<Connection>>> connect(ConnectionOptions options);
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;
  ~Connection();

  asio::awaitable<Result<Rows>> query(Query query, Duration timeout = Duration::zero());
  asio::awaitable<Result<void>> prepare(std::string name, std::string sql, std::vector<std::uint32_t> types = {});
  asio::awaitable<Result<Rows>> execute(std::string name, std::vector<Parameter> values = {});
  /// One implicit transaction per bounded batch. On an error, consume through PIPELINE_SYNC
  /// before leaving pipeline mode; the connection remains usable after SQL errors.
  asio::awaitable<Result<std::vector<Rows>>> pipeline(std::vector<Query> queries, Duration timeout = Duration::zero());
  /// COPY text/binary data supplied by the caller; identifiers in sql are trusted constants.
  asio::awaitable<Result<void>> copy_in(std::string sql, std::vector<std::string> chunks);
  /// Call LISTEN through query first; use a dedicated connection for notifications.
  asio::awaitable<Result<Notification>> notification(Duration timeout);

  void close() noexcept;
  [[nodiscard]] bool reusable() const noexcept;
  [[nodiscard]] int backend_pid() const noexcept;

 private:
  Connection(asio::any_io_executor executor, ConnectionOptions options);
  using Deadline = std::chrono::steady_clock::time_point;
  asio::awaitable<Result<void>> open();
  asio::awaitable<Result<void>> wait(bool read, bool write, Deadline end);
  asio::awaitable<Result<void>> flush(Deadline end);
  asio::awaitable<Result<pg_result*>> receive(Deadline end);
  asio::awaitable<Result<Rows>> finish(Deadline end);
  [[nodiscard]] static Error server_error(const pg_result* result);
  [[nodiscard]] Deadline deadline(Duration timeout) const;
  asio::any_io_executor executor_;
  ConnectionOptions options_;
  pg_conn* connection_ = nullptr;
  bool busy_ = false;
};

/// Reuse a provisioned namespace without requiring CREATE on the database. Callers serialize
/// schema initialization with their migration lock and retain ownership of their tables.
asio::awaitable<Result<void>> ensure_schema(Connection* connection, std::string schema);

}  // namespace psim::platform::pg
