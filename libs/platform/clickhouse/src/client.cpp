#include "psim/platform/clickhouse/client.hpp"

#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/this_coro.hpp>
#include <clickhouse/block.h>
#include <clickhouse/client.h>
#include <clickhouse/exceptions.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/credits.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/clickhouse/metrics.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/secret.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::clickhouse {
namespace {
struct Session {
  std::shared_ptr<asio::ssl::context> tls{};
  std::unique_ptr<::clickhouse::Client> client{};
};

Result<Session> connect(const PoolOptions& options, std::size_t endpoint) {
  ::clickhouse::ClientOptions native;
  native.SetHost(options.endpoints[endpoint].host)
      .SetPort(options.endpoints[endpoint].port)
      .SetDefaultDatabase(options.database)
      .SetUser(options.user)
      .SetSendRetries(0)
      .SetConnectionConnectTimeout(options.connect_timeout)
      .SetConnectionRecvTimeout(options.socket_timeout)
      .SetConnectionSendTimeout(options.socket_timeout)
      .SetCompressionMethod(::clickhouse::CompressionMethod::LZ4);
  if (!options.password_file.empty()) {
    auto password = security::read_secret(options.password_file);
    if (!password) {
      return std::unexpected(password.error());
    }
    native.SetPassword(std::string(password->view()));
  }
  Session session;
  if (!options.allow_plaintext) {
    auto context = security::tls_context(options.tls);
    if (!context) {
      return std::unexpected(context.error());
    }
    session.tls = std::move(*context);
    ::clickhouse::ClientOptions::SSLOptions tls;
    tls.SetExternalSSLContext(session.tls->native_handle()).SetUseSNI(true).SetSkipVerification(false);
    native.SetSSLOptions(tls);
  }
  session.client = std::make_unique<::clickhouse::Client>(native);
  return session;
}

bool valid(const PoolOptions& options) {
  return !options.endpoints.empty() && options.size > 0 && options.size <= 1024 && detail::identifier(options.database)
         && !options.user.empty() && options.acquire_timeout.count() > 0 && options.connect_timeout.count() > 0
         && options.socket_timeout.count() > 0 && options.query_timeout.count() > 0
         && options.query_timeout.count() <= 3600000 && options.max_result_rows > 0 && options.max_result_bytes > 0
         && std::ranges::all_of(options.endpoints,
                                [](const Endpoint& endpoint) { return !endpoint.host.empty() && endpoint.port != 0; });
}
}  // namespace

struct Pool::State {
  State(async::BlockingPool* executor, PoolOptions config, Metrics* observers)
      : blocking(executor), options(std::move(config)), metrics(observers), capacity(options.size) {}

  async::BlockingPool* blocking;
  PoolOptions options;
  Metrics* metrics;
  async::Semaphore capacity;
  std::vector<std::unique_ptr<Session>> idle{};
  std::size_t next_endpoint = 0;
  bool closed = false;

  template <typename T, typename Function>
  asio::awaitable<Result<T>> run(Function function) {
    if (closed || blocking == nullptr || !valid(options)) {
      co_return fail(ErrorCode::kCommonInvalidArgument, "invalid or closed ClickHouse pool");
    }
    async::SystemClock clock;
    auto acquired = co_await async::with_deadline(&clock, options.acquire_timeout, capacity.acquire());
    if (!acquired) {
      co_return std::unexpected(acquired.error());
    }
    if (!*acquired || closed) {
      co_return fail(ErrorCode::kCommonUnavailable, "ClickHouse pool is closed");
    }
    std::shared_ptr<Session> session;
    if (!idle.empty()) {
      session = std::move(idle.back());
      idle.pop_back();
    } else {
      session = std::make_shared<Session>();
    }
    const auto endpoint = next_endpoint % options.endpoints.size();
    if (!session->client) {
      ++next_endpoint;
    }
    // Native I/O cannot be interrupted through Asio. Drain it within socket/server bounds
    // before returning its lease; cancellation must never hand a live socket to another caller.
    const auto incoming = co_await asio::this_coro::cancellation_state;
    if (incoming.cancelled() != asio::cancellation_type::none) {
      co_return fail(ErrorCode::kCommonUnavailable, "ClickHouse operation cancelled before native I/O");
    }
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    co_await asio::this_coro::reset_cancellation_state([cancelled](asio::cancellation_type type) {
      if (type != asio::cancellation_type::none) {
        cancelled->store(true, std::memory_order_relaxed);
      }
      return asio::cancellation_type::none;
    });
    auto result = co_await blocking->run([session, config = options, endpoint,
                                          fn = std::move(function)]() mutable -> Result<T> {
      try {
        if (!session->client) {
          auto opened = connect(config, endpoint);
          if (!opened) {
            return std::unexpected(opened.error());
          }
          *session = std::move(*opened);
        }
        return fn(*session->client);
      } catch (const ::clickhouse::ServerException& error) {
        const int code = error.GetCode();
        if (code == 159) {
          return fail(ErrorCode::kCommonDeadlineExceeded, "ClickHouse query deadline exceeded");
        }
        if (code == 241 || code == 396) {
          return fail(ErrorCode::kCommonRateLimited, "ClickHouse query exceeded resource limit");
        }
        if (code == 516 || code == 497) {
          return fail(ErrorCode::kAuthForbidden, "ClickHouse authentication or authorization failed");
        }
        return fail(ErrorCode::kCommonUnavailable, "ClickHouse rejected operation (code " + std::to_string(code) + ")");
      } catch (const ::clickhouse::ValidationError&) {
        return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse native operation");
      } catch (const std::exception&) {
        return fail(ErrorCode::kCommonUnavailable,
                    "ClickHouse transport or protocol failure; write outcome may be unknown");
      }
    });
    co_await asio::this_coro::reset_cancellation_state();
    if (result && !closed) {
      idle.push_back(std::make_unique<Session>(std::move(*session)));
    }
    if (cancelled->load(std::memory_order_relaxed)) {
      co_return fail(ErrorCode::kCommonUnavailable, "ClickHouse operation cancelled after native I/O drained");
    }
    co_return result;
  }
};

Pool::Pool(async::BlockingPool* blocking, PoolOptions options, Metrics* metrics)
    : state_(std::make_shared<State>(blocking, std::move(options), metrics)) {}

Pool::~Pool() {
  close();
}

void Pool::close() {
  state_->closed = true;
  state_->capacity.close();
  state_->idle.clear();
}

asio::awaitable<Result<ResultSet>> Pool::select(Select query) {
  auto state = state_;
  auto native = detail::select_query(query, state->options);
  if (!native) {
    co_return std::unexpected(native.error());
  }
  const auto started = std::chrono::steady_clock::now();
  auto result = co_await state->run<ResultSet>(
      [statement = *native, config = state->options](::clickhouse::Client& client) mutable -> Result<ResultSet> {
        ResultSet rows;
        std::optional<Error> failure;
        const auto deadline = std::chrono::steady_clock::now() + config.query_timeout;
        statement.OnDataCancelable([&](const ::clickhouse::Block& block) {
          if (block.GetRowCount() == 0) {
            return true;
          }
          if (std::chrono::steady_clock::now() >= deadline) {
            failure.emplace(ErrorCode::kCommonDeadlineExceeded, "ClickHouse query deadline exceeded");
            return false;
          }
          auto bytes = detail::block_bytes(block, config.max_result_bytes - rows.bytes);
          if (!bytes || block.GetRowCount() > config.max_result_rows - rows.rows) {
            failure.emplace(ErrorCode::kCommonRateLimited, "ClickHouse result exceeds configured limit");
            return false;
          }
          rows.bytes += *bytes;
          rows.rows += block.GetRowCount();
          rows.blocks.push_back(block);
          return true;
        });
        client.Select(statement);
        if (failure) {
          return std::unexpected(*failure);
        }
        return rows;
      });
  if (state->metrics != nullptr) {
    state->metrics->query(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
                          result.has_value());
  }
  co_return result;
}

asio::awaitable<Result<void>> Pool::insert(std::string table, ::clickhouse::Block block, std::string token) {
  auto state = state_;
  if (!detail::identifier(table) || token.size() != 64
      || !std::ranges::all_of(token, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); })
      || block.GetRowCount() == 0) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid ClickHouse insertion or deduplication token");
  }
  auto serialized = detail::block_bytes(block, state->options.max_result_bytes);
  if (!serialized) {
    co_return std::unexpected(serialized.error());
  }
  const auto rows = block.GetRowCount();
  const auto started = std::chrono::steady_clock::now();
  auto result = co_await state->run<void>([table = std::move(table), block,
                                           token = std::move(token)](::clickhouse::Client& client) -> Result<void> {
    // psim-arch: allow-sql (table/columns are validated identifiers; token is exactly 64 hex digits)
    std::string statement = "INSERT INTO `" + table + "` (";
    for (std::size_t index = 0; index < block.GetColumnCount(); ++index) {
      if (index != 0) {
        statement += ", ";
      }
      statement += "`" + block.GetColumnName(index) + "`";
    }
    statement += ") SETTINGS insert_deduplicate=1, async_insert=0, insert_deduplication_token='" + token + "' VALUES";
    (void)client.BeginInsert(statement);
    client.SendInsertBlock(block);
    client.EndInsert();
    return {};
  });
  if (state->metrics != nullptr) {
    state->metrics->batch(rows, std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
                          result.has_value());
  }
  co_return result;
}

asio::awaitable<Result<void>> Pool::execute_ddl(std::string statement) {
  auto state = state_;
  co_return co_await state->run<void>([statement = std::move(statement), timeout = state->options.query_timeout](
                                          ::clickhouse::Client& client) -> Result<void> {
    ::clickhouse::Query query(statement);
    query.SetSetting("max_execution_time",
                     {std::to_string(std::max<std::int64_t>(1, (timeout.count() + 999) / 1000)), 1});
    client.Execute(query);
    return {};
  });
}
}  // namespace psim::platform::clickhouse
