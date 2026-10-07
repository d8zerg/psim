#include "psim/platform/async/shards.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <set>
#include <thread>

namespace {

namespace asio = boost::asio;
using psim::platform::async::available_cpus;
using psim::platform::async::Shards;

TEST(Shards, RunWorkOnOneThreadPerShard) {
  Shards shards(4);
  shards.start();
  std::mutex mutex;
  std::set<std::thread::id> threads;
  std::atomic<int> done{0};
  for (std::size_t i = 0; i < shards.size(); ++i) {
    shards.post(i, [&] {
      const std::scoped_lock lock(mutex);
      threads.insert(std::this_thread::get_id());
      ++done;
    });
  }
  while (done < 4) {
    std::this_thread::yield();
  }
  shards.stop();

  EXPECT_EQ(threads.size(), 4U);
  EXPECT_FALSE(threads.contains(std::this_thread::get_id()));
  EXPECT_EQ(shards.index_for(13), 1U);
  EXPECT_EQ(shards.index_for(13), shards.index_for(13));
}

// Shard-local counters are touched only on their own shard; shards talk by posting (TSan-checked).
TEST(Shards, ExchangeMessagesWithoutSharedState) {
  constexpr int kRounds = 10'000;
  Shards shards(2);
  shards.start();
  int pings = 0;  // shard 0 only
  int pongs = 0;  // shard 1 only
  std::promise<void> finished;
  std::function<void()> ping;
  ping = [&] {
    if (++pings == kRounds) {
      finished.set_value();
      return;
    }
    shards.post(1, [&] {
      ++pongs;
      shards.post(0, ping);
    });
  };
  shards.post(0, ping);
  finished.get_future().wait();
  shards.stop();

  EXPECT_EQ(pings, kRounds);
  EXPECT_EQ(pongs, kRounds - 1);
}

asio::awaitable<void> forever(asio::any_io_executor executor) {
  asio::steady_timer timer(executor, asio::steady_timer::time_point::max());
  co_await timer.async_wait(asio::use_awaitable);
}

TEST(Shards, StopEndsAShardThatAComponentLeftBusy) {
  Shards shards(1);
  shards.start();
  asio::co_spawn(shards.executor(0), forever(shards.executor(0)), asio::detached);

  const auto started = std::chrono::steady_clock::now();
  shards.stop(std::chrono::milliseconds{50});

  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{5});
  shards.stop();  // idempotent
}

TEST(Shards, DefaultToTheAvailableCpus) {
  EXPECT_GE(available_cpus(), 1U);
  const Shards shards(0);
  EXPECT_EQ(shards.size(), available_cpus());
}

}  // namespace
