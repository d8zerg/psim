#pragma once

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::async {

namespace asio = boost::asio;

/// Threads for blocking calls (ADR-016): RocksDB on large batches, file I/O, DNS, synchronous
/// clients. At most `max_tasks` calls run or wait in the pool; a caller beyond that waits for room
/// (backpressure, SR-18). The result returns to the caller's executor (its shard).
class BlockingPool {
 public:
  /// Value of a call: T for a function returning T or Result<T>.
  template <typename Value>
  struct Unwrap {
    using Type = Value;
  };

  template <typename Value>
  struct Unwrap<Result<Value>> {
    using Type = Value;
  };

  template <typename Value>
  using Unwrapped = typename Unwrap<Value>::Type;

  BlockingPool(std::size_t threads, std::size_t max_tasks);
  BlockingPool(const BlockingPool&) = delete;
  BlockingPool& operator=(const BlockingPool&) = delete;
  BlockingPool(BlockingPool&&) = delete;
  BlockingPool& operator=(BlockingPool&&) = delete;
  ~BlockingPool();

  /// Run `function` on the pool. An exception of a blocking library becomes COMMON_INTERNAL; a
  /// function returning Result<T> keeps its own errors.
  template <typename Function, typename Value = std::invoke_result_t<Function>>
  asio::awaitable<Result<Unwrapped<Value>>> run(Function function) {
    // After stop() the pool threads are gone: nothing would complete a wait on the slot channel.
    if (stopped_.load(std::memory_order_acquire)) {
      co_return fail(ErrorCode::kCommonUnavailable, "blocking pool is stopped");
    }
    if (auto [ec] = co_await slots_.async_receive(asio::as_tuple(asio::use_awaitable)); ec) {
      co_return fail(ErrorCode::kCommonUnavailable, "blocking pool is stopped");
    }
    Result<Unwrapped<Value>> result = fail(ErrorCode::kCommonInternal, "blocking call did not complete");
    try {
      result = co_await asio::co_spawn(pool_.get_executor(), call(std::move(function)), asio::use_awaitable);
    } catch (const std::exception& e) {
      result = fail(ErrorCode::kCommonInternal, std::string("blocking call failed: ") + e.what());
    }
    (void)slots_.try_send(boost::system::error_code{});
    co_return result;
  }

  /// Stop accepting calls and join the threads after the running ones finish.
  void stop();

 private:
  template <typename Function, typename Value = std::invoke_result_t<Function>>
  static asio::awaitable<Result<Unwrapped<Value>>> call(Function function) {
    if constexpr (std::is_void_v<Value>) {
      function();
      co_return Result<void>{};
    } else {
      co_return function();
    }
  }

  std::atomic<bool> stopped_{false};
  asio::thread_pool pool_;
  asio::experimental::concurrent_channel<void(boost::system::error_code)> slots_;
};

}  // namespace psim::platform::async
