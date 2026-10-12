#include "psim/platform/redis/pool.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <utility>
#include <vector>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/credits.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/redis/connection.hpp"

namespace psim::platform::redis {
struct Pool::State {
  explicit State(PoolOptions config) : options(std::move(config)), capacity(options.size) {
    idle.reserve(std::min<std::size_t>(options.size, 1024));
  }

  PoolOptions options;
  async::Semaphore capacity;
  std::vector<std::unique_ptr<Connection>> idle;
  bool closed = false;
};

Pool::Pool(PoolOptions options) : state_(std::make_shared<State>(std::move(options))) {}

Pool::~Pool() {
  close();
}

void Pool::close() {
  state_->closed = true;
  state_->capacity.close();
  state_->idle.clear();
}

std::size_t Pool::idle() const noexcept {
  return state_->idle.size();
}

Pool::Lease::Lease(std::shared_ptr<State> state, std::unique_ptr<Connection> connection,
                   async::Semaphore::Permit permit)
    : state_(std::move(state)), connection_(std::move(connection)), permit_(std::move(permit)) {}

Pool::Lease::Lease(Lease&& other) noexcept = default;

Pool::Lease& Pool::Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    release();
    state_ = std::move(other.state_);
    connection_ = std::move(other.connection_);
    permit_ = std::move(other.permit_);
  }
  return *this;
}

Pool::Lease::~Lease() {
  release();
}

void Pool::Lease::release() noexcept {
  if (!state_) {
    return;
  }
  if (connection_ && !state_->closed && connection_->reusable()) {
    state_->idle.push_back(std::move(connection_));
  }
  connection_.reset();
  permit_.release();
  state_.reset();
}

asio::awaitable<Result<Pool::Lease>> Pool::acquire(Duration timeout) {
  co_await asio::this_coro::throw_if_cancelled(false);
  auto state = state_;
  if ((state->options.size == 0 || state->options.size > 1024) || state->options.acquire_timeout <= Duration::zero()) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis pool options");
  }
  if (state->closed) {
    co_return fail(ErrorCode::kCommonUnavailable, "Redis pool closed");
  }
  if (timeout <= Duration::zero()) {
    timeout = state->options.acquire_timeout;
  }
  const auto end = std::chrono::steady_clock::now() + timeout;
  async::SystemClock clock;
  auto permit = co_await async::with_deadline(&clock, timeout, state->capacity.acquire());
  if (!permit) {
    co_return std::unexpected(permit.error());
  }
  if (!*permit || state->closed) {
    co_return fail(ErrorCode::kCommonUnavailable, "Redis pool acquisition interrupted");
  }
  std::unique_ptr<Connection> connection;
  if (!state->idle.empty()) {
    connection = std::move(state->idle.back());
    state->idle.pop_back();
  }
  if (connection && connection->reusable()) {
    const auto remaining = end - std::chrono::steady_clock::now();
    if (remaining <= Duration::zero()) {
      co_return fail(ErrorCode::kCommonDeadlineExceeded, "Redis pool acquisition deadline exceeded");
    }
    auto role = co_await connection->verify_primary(remaining);
    if (!role) {
      connection.reset();
    }
  }
  if (!connection || !connection->reusable()) {
    auto options = state->options.connection;
    const auto remaining = std::chrono::duration_cast<Duration>(end - std::chrono::steady_clock::now());
    if (remaining <= Duration::zero()) {
      co_return fail(ErrorCode::kCommonDeadlineExceeded, "Redis pool acquisition deadline exceeded");
    }
    options.connect_timeout = std::min(options.connect_timeout, remaining);
    auto connected = co_await Connection::connect(std::move(options));
    if (!connected) {
      co_return std::unexpected(connected.error());
    }
    connection = std::move(*connected);
  }
  if (state->closed) {
    co_return fail(ErrorCode::kCommonUnavailable, "Redis pool closed");
  }
  co_return Lease(std::move(state), std::move(connection), std::move(*permit));
}
}  // namespace psim::platform::redis
