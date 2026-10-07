#include "psim/platform/async/clock.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "psim/platform/async/deadline.hpp"
#include "support.hpp"

namespace {

namespace asio = boost::asio;
using psim::platform::async::Duration;
using psim::platform::async::ManualClock;
using psim::platform::async::spawn_cancellable;
using psim::platform::async::SystemClock;
using psim::platform::async::testing::run;
using psim::platform::async::testing::Spawned;

using Milliseconds = std::chrono::milliseconds;

asio::awaitable<bool> sleeper(ManualClock* clock, Milliseconds delay, std::vector<std::string>* order,
                              std::string name) {
  const bool woke = co_await clock->sleep_for(delay);
  order->push_back(name);
  co_return woke;
}

TEST(ManualClock, WakesSleepersInDeadlineOrderAndTiesInArrivalOrder) {
  asio::io_context io;
  ManualClock clock;
  std::vector<std::string> order;
  const Spawned<bool> a(io, sleeper(&clock, Milliseconds{30}, &order, "a"));
  const Spawned<bool> b(io, sleeper(&clock, Milliseconds{10}, &order, "b"));
  const Spawned<bool> c(io, sleeper(&clock, Milliseconds{10}, &order, "c"));
  io.poll();
  EXPECT_EQ(clock.sleepers(), 3U);

  clock.advance(Milliseconds{9});
  io.poll();
  EXPECT_TRUE(order.empty());

  clock.advance(Milliseconds{1});
  io.poll();
  EXPECT_EQ(order, (std::vector<std::string>{"b", "c"}));

  EXPECT_TRUE(clock.advance_to_next());
  io.poll();
  EXPECT_EQ(order, (std::vector<std::string>{"b", "c", "a"}));
  EXPECT_TRUE(a.value() && b.value() && c.value());
  EXPECT_FALSE(clock.advance_to_next());
}

TEST(ManualClock, MovesWallAndSteadyTimeTogether) {
  ManualClock clock;
  const auto wall = clock.wall();
  const auto steady = clock.steady();
  clock.advance(Milliseconds{1500});
  EXPECT_EQ(clock.wall() - wall, Milliseconds{1500});
  EXPECT_EQ(clock.steady() - steady, Milliseconds{1500});
  EXPECT_TRUE(run(clock.sleep_until(steady)));  // a deadline in the past does not wait
}

TEST(ManualClock, ACancelledSleeperLeavesTheClock) {
  asio::io_context io;
  ManualClock clock;
  std::vector<std::string> order;
  const auto handle = spawn_cancellable(io.get_executor(), sleeper(&clock, Milliseconds{10}, &order, "x"));
  io.poll();
  ASSERT_EQ(clock.sleepers(), 1U);

  handle.cancel();
  io.poll();

  EXPECT_EQ(clock.sleepers(), 0U);
  EXPECT_EQ(order, std::vector<std::string>{"x"});  // resumed early by the cancellation
}

TEST(SystemClock, SleepsInRealTime) {
  SystemClock clock;
  const auto started = clock.steady();
  EXPECT_TRUE(run(clock.sleep_for(Milliseconds{5})));
  EXPECT_GE(clock.steady() - started, Milliseconds{5});
  EXPECT_LE(clock.wall() - std::chrono::system_clock::now(), Duration{0});
}

}  // namespace
