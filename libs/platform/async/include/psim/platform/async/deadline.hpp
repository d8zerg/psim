#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::async {

/// Run `operation` with a deadline on `clock` (ADR-016). When the deadline comes first the
/// operation is cancelled (its pending waits complete with operation_aborted) and the result is
/// COMMON_DEADLINE_EXCEEDED.
template <typename T>
asio::awaitable<Result<T>> with_deadline(Clock* clock, Duration timeout, asio::awaitable<T> operation) {
  using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator||
  auto outcome = co_await (std::move(operation) || clock->sleep_for(timeout));
  if (outcome.index() == 1) {
    co_return fail(ErrorCode::kCommonDeadlineExceeded,
                   "deadline of "
                       + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count())
                       + " ms exceeded");
  }
  if constexpr (std::is_void_v<T>) {
    co_return Result<void>{};
  } else {
    co_return std::move(std::get<0>(outcome));
  }
}

/// A coroutine started with spawn_cancellable; cancel() asks it to stop at its next wait.
class Cancellable {
 public:
  Cancellable(asio::any_io_executor executor, std::shared_ptr<asio::cancellation_signal> signal)
      : executor_(std::move(executor)), signal_(std::move(signal)) {}

  /// Thread-safe: the signal is emitted on the coroutine's executor.
  void cancel(asio::cancellation_type type = asio::cancellation_type::terminal) const {
    asio::dispatch(executor_, [signal = signal_, type] { signal->emit(type); });
  }

 private:
  asio::any_io_executor executor_;
  std::shared_ptr<asio::cancellation_signal> signal_;
};

/// Start `operation` on `executor` with a cancellation handle. Exceptions are not expected from
/// platform coroutines (errors are values); one that escapes terminates the process.
template <typename T>
Cancellable spawn_cancellable(const asio::any_io_executor& executor, asio::awaitable<T> operation) {
  auto signal = std::make_shared<asio::cancellation_signal>();
  asio::co_spawn(executor, std::move(operation),
                 asio::bind_cancellation_slot(signal->slot(), [signal](const std::exception_ptr& error, auto&&...) {
                   if (error) {
                     std::rethrow_exception(error);
                   }
                 }));
  return {executor, std::move(signal)};
}

}  // namespace psim::platform::async
