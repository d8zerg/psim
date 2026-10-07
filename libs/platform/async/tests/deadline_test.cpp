#include "psim/platform/async/deadline.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <chrono>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "support.hpp"

namespace {

namespace asio = boost::asio;
using psim::platform::ErrorCode;
using psim::platform::Result;
using psim::platform::async::Clock;
using psim::platform::async::ManualClock;
using psim::platform::async::spawn_cancellable;
using psim::platform::async::SystemClock;
using psim::platform::async::with_deadline;
using psim::platform::async::testing::run;
using psim::platform::async::testing::Spawned;

using Milliseconds = std::chrono::milliseconds;

// An operation that takes `work` on `clock` and reports whether it finished or was cancelled.
asio::awaitable<int> work(Clock* clock, Milliseconds duration, bool* cancelled) {
  if (!co_await clock->sleep_for(duration)) {
    *cancelled = true;
  }
  co_return 42;
}

asio::awaitable<void> wait(Clock* clock, Milliseconds duration) {
  co_await clock->sleep_for(duration);
}

TEST(WithDeadline, ReturnsTheValueOfAnOperationInTime) {
  SystemClock clock;
  bool cancelled = false;
  const auto result = run(with_deadline(&clock, Milliseconds{1000}, work(&clock, Milliseconds{1}, &cancelled)));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
  EXPECT_FALSE(cancelled);
}

TEST(WithDeadline, CancelsALateOperationDeterministically) {
  asio::io_context io;
  ManualClock clock;
  bool cancelled = false;
  const Spawned<Result<int>> result(
      io, with_deadline(&clock, Milliseconds{20}, work(&clock, Milliseconds{50}, &cancelled)));
  io.poll();

  clock.advance(Milliseconds{20});
  io.poll();

  ASSERT_TRUE(result.done());
  ASSERT_FALSE(result.value().has_value());
  EXPECT_EQ(result.value().error().code(), ErrorCode::kCommonDeadlineExceeded);
  EXPECT_EQ(result.value().error().message(), "deadline of 20 ms exceeded");
  EXPECT_TRUE(cancelled);
  EXPECT_EQ(clock.sleepers(), 0U);
}

TEST(WithDeadline, SupportsVoidOperations) {
  SystemClock clock;
  EXPECT_TRUE(run(with_deadline(&clock, Milliseconds{1000}, wait(&clock, Milliseconds{1}))).has_value());
  const auto late = run(with_deadline(&clock, Milliseconds{1}, wait(&clock, Milliseconds{1000})));
  ASSERT_FALSE(late.has_value());
  EXPECT_EQ(late.error().code(), ErrorCode::kCommonDeadlineExceeded);
}

TEST(SpawnCancellable, StopsACoroutineAtItsNextWait) {
  asio::io_context io;
  SystemClock clock;
  bool cancelled = false;
  const auto handle = spawn_cancellable(io.get_executor(), work(&clock, Milliseconds{60'000}, &cancelled));
  io.poll();

  const auto started = std::chrono::steady_clock::now();
  handle.cancel();
  io.run();

  EXPECT_TRUE(cancelled);
  EXPECT_LT(std::chrono::steady_clock::now() - started, Milliseconds{5000});
}

}  // namespace
