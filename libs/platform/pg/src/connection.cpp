#include "psim/platform/pg/connection.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <libpq-fe.h>
#include <postgres_ext.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::pg {
namespace {
class Busy {
 public:
  explicit Busy(bool* flag) : flag_(flag) { *flag_ = true; }

  Busy(const Busy&) = delete;
  Busy& operator=(const Busy&) = delete;

  Busy(Busy&&) = delete;
  Busy& operator=(Busy&&) = delete;

  ~Busy() { *flag_ = false; }

 private:
  bool* flag_;
};

struct Parameters {
  std::vector<const char*> values;
  std::vector<int> lengths;
  std::vector<int> formats;
  std::vector<Oid> types;

  explicit Parameters(const std::vector<Parameter>& parameters) {
    for (const auto& parameter : parameters) {
      values.push_back(parameter.value ? parameter.value->c_str() : nullptr);
      lengths.push_back(parameter.value ? static_cast<int>(parameter.value->size()) : 0);
      formats.push_back(parameter.binary ? 1 : 0);
      types.push_back(parameter.type);
    }
  }
};

constexpr std::size_t kMaxRequestBytes = std::size_t{16} * 1024 * 1024;

std::size_t request_bytes(const Query& query) {
  std::size_t bytes = query.sql.size();
  for (const auto& parameter : query.parameters) {
    const auto size = parameter.value ? parameter.value->size() : 0;
    if (size > kMaxRequestBytes || bytes > kMaxRequestBytes - size) {
      return kMaxRequestBytes + 1;
    }
    bytes += size;
  }
  return bytes;
}

bool valid(const Query& query) {
  return !query.sql.empty() && query.sql.size() <= std::size_t{1024} * 1024 && query.sql.find('\0') == std::string::npos
         && query.parameters.size() <= 65535 && request_bytes(query) <= kMaxRequestBytes
         && std::ranges::all_of(query.parameters, [](const Parameter& parameter) {
              return !parameter.value || parameter.binary || parameter.value->find('\0') == std::string::npos;
            });
}

Error unavailable() {
  return {ErrorCode::kCommonUnavailable, "PostgreSQL connection unavailable"};
}

asio::awaitable<boost::system::error_code> socket_ready(asio::posix::stream_descriptor* descriptor, bool read,
                                                        bool write) {
  using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator||
  if (read && write) {
    auto ready = co_await (
        descriptor->async_wait(asio::posix::stream_descriptor::wait_read, asio::as_tuple(asio::use_awaitable))
        || descriptor->async_wait(asio::posix::stream_descriptor::wait_write, asio::as_tuple(asio::use_awaitable)));
    co_return ready.index() == 0 ? std::get<0>(std::get<0>(ready)) : std::get<0>(std::get<1>(ready));
  }
  const auto [error] = co_await descriptor->async_wait(
      read ? asio::posix::stream_descriptor::wait_read : asio::posix::stream_descriptor::wait_write,
      asio::as_tuple(asio::use_awaitable));
  co_return error;
}

bool successful(ExecStatusType status) {
  return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}
}  // namespace

Parameter Parameter::text(std::string value, std::uint32_t type) {
  return {.value = std::move(value), .type = type};
}

Parameter Parameter::integer(std::int64_t value) {
  return text(std::to_string(value), 20);
}

Parameter Parameter::boolean(bool value) {
  return text(value ? "true" : "false", 16);
}

Parameter Parameter::bytes(std::string value) {
  return {.value = std::move(value), .type = 17, .binary = true};
}

Parameter Parameter::null(std::uint32_t type) {
  return {.type = type};
}

Rows::Rows(Rows&& other) noexcept : result_(std::exchange(other.result_, nullptr)) {}

Rows& Rows::operator=(Rows&& other) noexcept {
  if (this != &other) {
    PQclear(result_);
    result_ = std::exchange(other.result_, nullptr);
  }
  return *this;
}

Rows::~Rows() {
  PQclear(result_);
}

std::size_t Rows::size() const noexcept {
  return result_ ? static_cast<std::size_t>(PQntuples(result_)) : 0;
}

std::size_t Rows::columns() const noexcept {
  return result_ ? static_cast<std::size_t>(PQnfields(result_)) : 0;
}

std::optional<std::string_view> Rows::value(std::size_t row, std::size_t column) const noexcept {
  if (row >= size() || column >= columns()
      || PQgetisnull(result_, static_cast<int>(row), static_cast<int>(column)) != 0) {
    return std::nullopt;
  }
  return std::string_view(
      PQgetvalue(result_, static_cast<int>(row), static_cast<int>(column)),
      static_cast<std::size_t>(PQgetlength(result_, static_cast<int>(row), static_cast<int>(column))));
}

Result<std::int64_t> Rows::integer(std::size_t row, std::size_t column) const {
  const auto text = value(row, column);
  if (text) {
    std::int64_t out = 0;
    const auto parsed = std::from_chars(text->begin(), text->end(), out);
    if (parsed.ec == std::errc{} && parsed.ptr == text->end()) {
      return out;
    }
  }
  return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL cell is not an integer");
}

std::uint32_t Rows::column_type(std::size_t column) const noexcept {
  return column < columns() ? PQftype(result_, static_cast<int>(column)) : 0;
}

std::string_view Rows::column_name(std::size_t column) const noexcept {
  return column < columns() ? std::string_view(PQfname(result_, static_cast<int>(column))) : std::string_view{};
}

std::string_view Rows::command() const noexcept {
  return result_ ? std::string_view(PQcmdStatus(result_)) : std::string_view{};
}

std::uint64_t Rows::affected() const noexcept {
  std::uint64_t count = 0;
  if (result_) {
    const std::string_view text(PQcmdTuples(result_));
    std::from_chars(text.begin(), text.end(), count);
  }
  return count;
}

Connection::Connection(asio::any_io_executor executor, ConnectionOptions options)
    : executor_(std::move(executor)), options_(std::move(options)) {}

Connection::~Connection() {
  close();
}

void Connection::close() noexcept {
  PQfinish(connection_);
  connection_ = nullptr;
}

bool Connection::reusable() const noexcept {
  return connection_ != nullptr && !busy_ && PQstatus(connection_) == CONNECTION_OK
         && PQtransactionStatus(connection_) == PQTRANS_IDLE && PQpipelineStatus(connection_) == PQ_PIPELINE_OFF;
}

int Connection::backend_pid() const noexcept {
  return connection_ ? PQbackendPID(connection_) : 0;
}

Connection::Deadline Connection::deadline(Duration timeout) const {
  return std::chrono::steady_clock::now() + (timeout > Duration::zero() ? timeout : options_.query_timeout);
}

asio::awaitable<Result<std::unique_ptr<Connection>>> Connection::connect(ConnectionOptions options) {
  auto connection = std::unique_ptr<Connection>(new Connection(co_await asio::this_coro::executor, std::move(options)));
  auto opened = co_await connection->open();
  if (!opened) {
    co_return std::unexpected(opened.error());
  }
  co_return connection;
}

asio::awaitable<Result<void>> Connection::open() {
  co_await asio::this_coro::throw_if_cancelled(false);
  if (options_.host.empty() || options_.port.empty() || options_.connect_timeout <= Duration::zero()
      || options_.query_timeout <= Duration::zero() || options_.max_result_bytes == 0) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL connection options");
  }
  const auto end = std::chrono::steady_clock::now() + options_.connect_timeout;
  auto properties = options_.properties;
  for (const auto* key : {"host", "port", "dbname", "user", "password"}) {
    if (properties.contains(key)) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL connection identity belongs in explicit options");
    }
  }
  properties["host"] = options_.host;
  properties["port"] = options_.port;
  properties["dbname"] = options_.database;
  properties["user"] = options_.user;
  properties["password"] = options_.password;
  properties.try_emplace("sslmode", "verify-full");
  properties.try_emplace("gssencmode", "disable");
  properties.try_emplace("target_session_attrs", "read-write");
  properties.try_emplace("application_name", "psim");
  properties.try_emplace("options", "-cstatement_timeout=30000 -cidle_in_transaction_session_timeout=30000");
  if (!properties.contains("hostaddr") && !options_.host.starts_with('/')) {
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator||
    asio::ip::tcp::resolver resolver(executor_);
    asio::steady_timer timer(executor_, end);
    auto resolved = co_await (resolver.async_resolve(options_.host, options_.port, asio::as_tuple(asio::use_awaitable))
                              || timer.async_wait(asio::as_tuple(asio::use_awaitable)));
    if (resolved.index() == 1) {
      co_return fail(ErrorCode::kCommonDeadlineExceeded, "PostgreSQL name resolution deadline exceeded");
    }
    const auto& [error, endpoints] = std::get<0>(resolved);
    if (error || endpoints.empty()) {
      co_return std::unexpected(unavailable());
    }
    properties["hostaddr"] = endpoints.begin()->endpoint().address().to_string();
  }
  std::vector<const char*> keys;
  std::vector<const char*> values;
  for (const auto& [key, value] : properties) {
    keys.push_back(key.c_str());
    values.push_back(value.c_str());
  }
  keys.push_back(nullptr);
  values.push_back(nullptr);
  connection_ = PQconnectStartParams(keys.data(), values.data(), 0);
  if (connection_ == nullptr || PQstatus(connection_) == CONNECTION_BAD) {
    close();
    co_return std::unexpected(unavailable());
  }
  PQsetNoticeProcessor(connection_, [](void*, const char*) {}, nullptr);  // notices may contain SQL values/secrets
  auto state = PGRES_POLLING_WRITING;
  while (state != PGRES_POLLING_OK) {
    auto ready = co_await wait(state == PGRES_POLLING_READING, state == PGRES_POLLING_WRITING, end);
    if (!ready) {
      co_return ready;
    }
    state = PQconnectPoll(connection_);
    if (state == PGRES_POLLING_FAILED) {
      close();
      co_return std::unexpected(unavailable());
    }
  }
  if (PQsetnonblocking(connection_, 1) != 0) {
    close();
    co_return std::unexpected(unavailable());
  }
  co_return Result<void>{};
}

asio::awaitable<Result<void>> Connection::wait(bool read, bool write, Deadline end) {
  co_await asio::this_coro::throw_if_cancelled(false);
  if (connection_ == nullptr || std::chrono::steady_clock::now() >= end) {
    close();
    co_return fail(ErrorCode::kCommonDeadlineExceeded, "PostgreSQL operation deadline exceeded");
  }
  const int duplicate = ::dup(PQsocket(connection_));
  if (duplicate < 0) {
    close();
    co_return std::unexpected(unavailable());
  }
  // Asio owns only the duplicate. libpq can replace its descriptor while connecting.
  asio::posix::stream_descriptor descriptor(executor_, duplicate);
  asio::steady_timer timer(executor_, end);
  using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator||
  auto outcome =
      co_await (socket_ready(&descriptor, read, write) || timer.async_wait(asio::as_tuple(asio::use_awaitable)));
  if (outcome.index() == 1 || std::get<0>(outcome)) {
    close();
    co_return fail(
        std::chrono::steady_clock::now() >= end ? ErrorCode::kCommonDeadlineExceeded : ErrorCode::kCommonUnavailable,
        "PostgreSQL operation interrupted");
  }
  co_return Result<void>{};
}

asio::awaitable<Result<void>> Connection::flush(Deadline end) {
  for (;;) {
    const int status = PQflush(connection_);
    if (status == 0) {
      co_return Result<void>{};
    }
    if (status < 0) {
      close();
      co_return std::unexpected(unavailable());
    }
    auto ready = co_await wait(true, true, end);
    if (!ready) {
      co_return ready;
    }
    if (PQconsumeInput(connection_) == 0) {
      close();
      co_return std::unexpected(unavailable());
    }
  }
}

asio::awaitable<Result<pg_result*>> Connection::receive(Deadline end) {
  while (PQisBusy(connection_) != 0) {
    auto ready = co_await wait(true, false, end);
    if (!ready) {
      co_return std::unexpected(ready.error());
    }
    if (PQconsumeInput(connection_) == 0) {
      close();
      co_return std::unexpected(unavailable());
    }
  }
  co_return PQgetResult(connection_);
}

Error Connection::server_error(const pg_result* result) {
  const char* field = PQresultErrorField(result, PG_DIAG_SQLSTATE);
  const std::string state = field ? field : "unknown";
  ErrorCode code = ErrorCode::kCommonInvalidArgument;
  if (state == "57014") {
    code = ErrorCode::kCommonDeadlineExceeded;
  } else if (state.starts_with("08") || state.starts_with("40") || state.starts_with("53") || state.starts_with("57")) {
    code = ErrorCode::kCommonUnavailable;
  } else if (state.starts_with("23")) {
    code = ErrorCode::kCommonConcurrentModification;
  }
  // Server diagnostics can include passwords, parameter values or SQL. Expose SQLSTATE only.
  return {code, "PostgreSQL statement failed (SQLSTATE " + state + ")"};
}

asio::awaitable<Result<Rows>> Connection::finish(Deadline end) {
  auto flushed = co_await flush(end);
  if (!flushed) {
    co_return std::unexpected(flushed.error());
  }
  std::optional<Rows> last;
  std::optional<Error> error;
  for (;;) {
    auto next = co_await receive(end);
    if (!next) {
      co_return std::unexpected(next.error());
    }
    if (!*next) {
      break;
    }
    Rows row(*next);
    if (!successful(PQresultStatus(*next))) {
      error = server_error(*next);
    } else if (PQresultMemorySize(*next) > options_.max_result_bytes) {
      error = Error(ErrorCode::kCommonInvalidArgument, "PostgreSQL result exceeds configured size limit");
    }
    last = std::move(row);
  }
  if (error) {
    co_return std::unexpected(*error);
  }
  if (!last) {
    co_return fail(ErrorCode::kCommonInternal, "PostgreSQL returned no result");
  }
  co_return std::move(*last);
}

asio::awaitable<Result<Rows>> Connection::query(Query query, Duration timeout) {
  if (busy_ || connection_ == nullptr) {
    co_return std::unexpected(unavailable());
  }
  if (!valid(query)) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL query or parameters");
  }
  const Busy guard(&busy_);
  Parameters parameters(query.parameters);
  if (PQsendQueryParams(connection_, query.sql.c_str(), static_cast<int>(query.parameters.size()),
                        parameters.types.data(), parameters.values.data(), parameters.lengths.data(),
                        parameters.formats.data(), 0)
      == 0) {
    close();
    co_return std::unexpected(unavailable());
  }
  co_return co_await finish(deadline(timeout));
}

asio::awaitable<Result<void>> Connection::prepare(std::string name, std::string sql, std::vector<std::uint32_t> types) {
  if (busy_ || connection_ == nullptr) {
    co_return std::unexpected(unavailable());
  }
  if (!valid({.sql = sql}) || name.empty() || name.find('\0') != std::string::npos || types.size() > 65535) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL prepared statement");
  }
  const Busy guard(&busy_);
  if (PQsendPrepare(connection_, name.c_str(), sql.c_str(), static_cast<int>(types.size()), types.data()) == 0) {
    close();
    co_return std::unexpected(unavailable());
  }
  const auto result = co_await finish(deadline(Duration::zero()));
  co_return result ? Result<void>{} : Result<void>(std::unexpected(result.error()));
}

asio::awaitable<Result<Rows>> Connection::execute(std::string name, std::vector<Parameter> values) {
  if (busy_ || connection_ == nullptr) {
    co_return std::unexpected(unavailable());
  }
  if (name.empty() || name.find('\0') != std::string::npos || !valid({.sql = name, .parameters = values})) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL prepared statement parameters");
  }
  const Busy guard(&busy_);
  Parameters parameters(values);
  if (PQsendQueryPrepared(connection_, name.c_str(), static_cast<int>(values.size()), parameters.values.data(),
                          parameters.lengths.data(), parameters.formats.data(), 0)
      == 0) {
    close();
    co_return std::unexpected(unavailable());
  }
  co_return co_await finish(deadline(Duration::zero()));
}

asio::awaitable<Result<std::vector<Rows>>> Connection::pipeline(std::vector<Query> queries, Duration timeout) {
  if (busy_ || connection_ == nullptr) {
    co_return std::unexpected(unavailable());
  }
  if (queries.empty() || queries.size() > 1024 || !std::ranges::all_of(queries, valid)) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL pipeline batch");
  }
  std::size_t request_size = 0;
  for (const auto& query : queries) {
    const auto bytes = request_bytes(query);
    if (bytes > kMaxRequestBytes - request_size) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "PostgreSQL pipeline request exceeds size limit");
    }
    request_size += bytes;
  }
  const Busy guard(&busy_);
  if (PQenterPipelineMode(connection_) == 0) {
    co_return std::unexpected(unavailable());
  }
  const auto end = deadline(timeout);
  for (const auto& query : queries) {
    Parameters parameters(query.parameters);
    if (PQsendQueryParams(connection_, query.sql.c_str(), static_cast<int>(query.parameters.size()),
                          parameters.types.data(), parameters.values.data(), parameters.lengths.data(),
                          parameters.formats.data(), 0)
        == 0) {
      close();
      co_return std::unexpected(unavailable());
    }
  }
  if (PQsendPipelineSync(connection_) == 0) {
    close();
    co_return std::unexpected(unavailable());
  }
  const auto flushed = co_await flush(end);
  if (!flushed) {
    co_return std::unexpected(flushed.error());
  }
  std::vector<Rows> results;
  std::optional<Error> error;
  std::size_t bytes = 0;
  for (;;) {
    auto next = co_await receive(end);
    if (!next) {
      co_return std::unexpected(next.error());
    }
    if (!*next) {
      continue;  // libpq inserts a NULL boundary after each query, before the final sync
    }
    const auto status = PQresultStatus(*next);
    if (status == PGRES_PIPELINE_SYNC) {
      PQclear(*next);
      break;
    }
    bytes += PQresultMemorySize(*next);
    if (!successful(status)) {
      if (!error) {
        error = server_error(*next);
      }
    } else if (bytes > options_.max_result_bytes) {
      error = Error(ErrorCode::kCommonInvalidArgument, "PostgreSQL pipeline results exceed configured size limit");
    }
    if (error) {
      PQclear(*next);
      results.clear();
    } else {
      results.emplace_back(*next);
    }
  }
  // A final NULL terminates the sync result before PQexitPipelineMode can succeed.
  PQclear(PQgetResult(connection_));
  if (PQexitPipelineMode(connection_) == 0) {
    close();
    co_return std::unexpected(unavailable());
  }
  if (error) {
    co_return std::unexpected(*error);
  }
  co_return results;
}

asio::awaitable<Result<void>> Connection::copy_in(std::string sql, std::vector<std::string> chunks) {
  if (busy_ || connection_ == nullptr) {
    co_return std::unexpected(unavailable());
  }
  if (!valid({.sql = sql}) || !std::ranges::all_of(chunks, [](const auto& chunk) {
        return chunk.size() <= static_cast<std::size_t>(INT_MAX);
      })) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid PostgreSQL COPY data");
  }
  const Busy guard(&busy_);
  const auto end = deadline(Duration::zero());
  if (PQsendQueryParams(connection_, sql.c_str(), 0, nullptr, nullptr, nullptr, nullptr, 0) == 0) {
    close();
    co_return std::unexpected(unavailable());
  }
  auto flushed = co_await flush(end);
  if (!flushed) {
    co_return flushed;
  }
  auto initial = co_await receive(end);
  if (!initial) {
    co_return std::unexpected(initial.error());
  }
  const Rows initial_result(*initial);
  if (!*initial || PQresultStatus(*initial) != PGRES_COPY_IN) {
    auto error = server_error(*initial);
    close();
    co_return std::unexpected(error);
  }
  for (const auto& chunk : chunks) {
    for (;;) {
      const int sent = PQputCopyData(connection_, chunk.data(), static_cast<int>(chunk.size()));
      if (sent == 1) {
        break;
      }
      if (sent < 0) {
        close();
        co_return std::unexpected(unavailable());
      }
      flushed = co_await flush(end);
      if (!flushed) {
        co_return flushed;
      }
    }
  }
  for (;;) {
    const int sent = PQputCopyEnd(connection_, nullptr);
    if (sent == 1) {
      break;
    }
    if (sent < 0) {
      close();
      co_return std::unexpected(unavailable());
    }
    flushed = co_await flush(end);
    if (!flushed) {
      co_return flushed;
    }
  }
  const auto result = co_await finish(end);
  co_return result ? Result<void>{} : Result<void>(std::unexpected(result.error()));
}

asio::awaitable<Result<Notification>> Connection::notification(Duration timeout) {
  if (busy_ || connection_ == nullptr || timeout <= Duration::zero()) {
    co_return std::unexpected(unavailable());
  }
  const Busy guard(&busy_);
  const auto end = deadline(timeout);
  for (;;) {
    if (auto* notice = PQnotifies(connection_)) {
      Notification result{.channel = notice->relname, .payload = notice->extra};
      PQfreemem(notice);
      co_return result;
    }
    auto ready = co_await wait(true, false, end);
    if (!ready) {
      co_return std::unexpected(ready.error());
    }
    if (PQconsumeInput(connection_) == 0) {
      close();
      co_return std::unexpected(unavailable());
    }
  }
}
}  // namespace psim::platform::pg
