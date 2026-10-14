#include "psim/sim/scheduler.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace {

using psim::sim::Scheduler;
using Milliseconds = std::chrono::milliseconds;

boost::asio::awaitable<void> sleeper(Scheduler* scheduler, std::vector<int>* order, int id, Milliseconds delay) {
  std::ignore = co_await scheduler->clock().sleep_for(delay);
  order->push_back(id);
}

boost::asio::awaitable<void> failing() {
  throw std::runtime_error("actor failure");
  co_return;
}

boost::asio::awaitable<void> blocked() {
  boost::asio::experimental::channel<void(boost::system::error_code, int)> channel(
      co_await boost::asio::this_coro::executor, 1);
  std::ignore = co_await channel.async_receive(boost::asio::use_awaitable);
}

TEST(SimScheduler, OrdersDeadlinesAndTiesWithoutWallTime) {
  std::vector<int> order;
  Scheduler scheduler;
  scheduler.spawn(sleeper(&scheduler, &order, 1, Milliseconds{20}));
  scheduler.spawn(sleeper(&scheduler, &order, 2, Milliseconds{5}));
  scheduler.spawn(sleeper(&scheduler, &order, 3, Milliseconds{5}));
  scheduler.run();
  EXPECT_EQ(order, (std::vector<int>{2, 3, 1}));
  EXPECT_EQ(scheduler.clock().steady().time_since_epoch(), Milliseconds{20});
  EXPECT_EQ(scheduler.clock().wall().time_since_epoch(), Milliseconds{20});
  EXPECT_EQ(scheduler.clock().sleepers(), 0U);
}

TEST(SimScheduler, ReportsActorFailuresAndDeadlocks) {
  Scheduler failure;
  failure.spawn(failing());
  EXPECT_THROW(failure.run(), std::runtime_error);
  Scheduler deadlock;
  deadlock.spawn(blocked());
  EXPECT_THROW(deadlock.run(), std::runtime_error);
}

TEST(SimScheduler, BoundsVirtualTimeAndReadyHandlers) {
  std::vector<int> order;
  Scheduler timed;
  timed.spawn(sleeper(&timed, &order, 1, Milliseconds{20}));
  EXPECT_THROW(timed.run({.handlers = 100, .virtual_time = Milliseconds{10}}), std::runtime_error);
  Scheduler bounded;
  bounded.spawn(sleeper(&bounded, &order, 2, Milliseconds{20}));
  EXPECT_THROW(bounded.run({.handlers = 1}), std::runtime_error);
  EXPECT_THROW(bounded.run({.handlers = 0}), std::invalid_argument);
  EXPECT_THROW(bounded.run({.virtual_time = Milliseconds{-1}}), std::invalid_argument);
}

}  // namespace
