#include "psim/platform/async/blocking_pool.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

#include "psim/platform/async/shards.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "support.hpp"

namespace {

namespace asio = boost::asio;
using psim::platform::ErrorCode;
using psim::platform::fail;
using psim::platform::Result;
using psim::platform::async::BlockingPool;
using psim::platform::async::Shards;
using psim::platform::async::testing::run;
using psim::platform::async::testing::Spawned;

struct Threads {
  std::thread::id caller;
  std::thread::id worker;
  std::thread::id resumed;
};

asio::awaitable<Threads> where(BlockingPool* pool) {
  Threads threads{.caller = std::this_thread::get_id(), .worker = {}, .resumed = {}};
  const auto worker = co_await pool->run([] { return std::this_thread::get_id(); });
  threads.worker = worker.value();
  threads.resumed = std::this_thread::get_id();
  co_return threads;
}

TEST(BlockingPool, RunsOnThePoolAndReturnsToTheCaller) {
  BlockingPool pool(2, 4);
  const auto threads = run(where(&pool));
  EXPECT_NE(threads.worker, threads.caller);
  EXPECT_EQ(threads.resumed, threads.caller);
}

asio::awaitable<Result<int>> throwing(BlockingPool* pool) {
  co_return co_await pool->run([]() -> int { throw std::runtime_error("disk full"); });
}

asio::awaitable<Result<int>> failing(BlockingPool* pool) {
  co_return co_await pool->run([]() -> Result<int> { return fail(ErrorCode::kCommonNotFound, "no such key"); });
}

asio::awaitable<Result<void>> nothing(BlockingPool* pool) {
  co_return co_await pool->run([] {});
}

TEST(BlockingPool, TurnsExceptionsIntoErrorsAndKeepsResults) {
  BlockingPool pool(1, 1);
  const auto thrown = run(throwing(&pool));
  ASSERT_FALSE(thrown.has_value());
  EXPECT_EQ(thrown.error().code(), ErrorCode::kCommonInternal);
  EXPECT_EQ(thrown.error().message(), "blocking call failed: disk full");

  const auto failed = run(failing(&pool));
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error().code(), ErrorCode::kCommonNotFound);

  EXPECT_TRUE(run(nothing(&pool)).has_value());
}

asio::awaitable<Result<int>> blocking(BlockingPool* pool, std::shared_future<void> gate, std::atomic<int>* started) {
  co_return co_await pool->run([gate, started] {
    ++*started;
    gate.wait();
    return 1;
  });
}

TEST(BlockingPool, HoldsCallersBeyondItsLimit) {
  BlockingPool pool(4, 1);
  std::promise<void> open;
  const std::shared_future<void> gate = open.get_future().share();
  std::atomic<int> started{0};
  asio::io_context io;
  const Spawned<Result<int>> first(io, blocking(&pool, gate, &started));
  const Spawned<Result<int>> second(io, blocking(&pool, gate, &started));
  std::thread driver([&] { io.run(); });

  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  EXPECT_EQ(started, 1);  // one slot: the second call waits although threads are free

  open.set_value();
  driver.join();
  EXPECT_EQ(started, 2);
  EXPECT_TRUE(first.value().has_value() && second.value().has_value());
}

asio::awaitable<int> sum_on_pool(BlockingPool* pool, int calls) {
  int sum = 0;
  for (int i = 0; i < calls; ++i) {
    sum += (co_await pool->run([] { return 1; })).value();
  }
  co_return sum;
}

TEST(BlockingPool, ServesSeveralShardsConcurrently) {
  BlockingPool pool(2, 3);
  Shards shards(4);
  shards.start();
  std::vector<std::future<int>> sums;
  sums.reserve(shards.size());
  for (std::size_t i = 0; i < shards.size(); ++i) {
    sums.push_back(asio::co_spawn(shards.executor(i), sum_on_pool(&pool, 200), asio::use_future));
  }
  int total = 0;
  for (auto& sum : sums) {
    total += sum.get();
  }
  shards.stop();
  EXPECT_EQ(total, 800);
}

TEST(BlockingPool, RefusesCallsAfterStop) {
  BlockingPool pool(1, 1);
  pool.stop();
  const auto refused = run(sum_on_pool(&pool, 0));
  EXPECT_EQ(refused, 0);
  const auto result = run(nothing(&pool));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::kCommonUnavailable);
}

}  // namespace
