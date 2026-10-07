// Overhead of the execution model primitives (B-02, ADR-016): the baseline every service pays.
// Each benchmark runs a batch of operations inside one coroutine and reports operations per second.

#include <benchmark/benchmark.h>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <cstdint>
#include <future>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/credits.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/async/queue.hpp"
#include "psim/platform/async/shards.hpp"

namespace {

namespace asio = boost::asio;
using psim::platform::async::BlockingPool;
using psim::platform::async::BoundedQueue;
using psim::platform::async::Credits;
using psim::platform::async::Shards;
using psim::platform::async::SystemClock;
using psim::platform::async::with_deadline;

constexpr int kBatch = 1000;

asio::awaitable<int> immediate() {
  co_return 1;
}

asio::awaitable<void> resume_batch() {
  int sum = 0;
  for (int i = 0; i < kBatch; ++i) {
    sum += co_await immediate();
  }
  benchmark::DoNotOptimize(sum);
}

void coroutine_resume(benchmark::State& state) {
  asio::io_context io;
  for ([[maybe_unused]] auto iteration : state) {
    asio::co_spawn(io, resume_batch(), asio::detached);
    io.run();
    io.restart();
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}

asio::awaitable<void> hop_batch(Shards* shards) {
  for (int i = 0; i < kBatch; ++i) {
    co_await asio::post(shards->executor(1), asio::use_awaitable);  // to the other shard
    co_await asio::post(shards->executor(0), asio::use_awaitable);  // and back
  }
}

void cross_shard_round_trip(benchmark::State& state) {
  Shards shards(2);
  shards.start();
  for ([[maybe_unused]] auto iteration : state) {
    asio::co_spawn(shards.executor(0), hop_batch(&shards), asio::use_future).get();
  }
  shards.stop();
  state.SetItemsProcessed(state.iterations() * kBatch);
}

asio::awaitable<void> credits_batch(Credits* credits) {
  for (int i = 0; i < kBatch; ++i) {
    credits->grant(1);
    benchmark::DoNotOptimize(co_await credits->acquire(1));
  }
}

void credits_grant_acquire(benchmark::State& state) {
  asio::io_context io;
  Credits credits;
  for ([[maybe_unused]] auto iteration : state) {
    asio::co_spawn(io, credits_batch(&credits), asio::detached);
    io.run();
    io.restart();
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}

asio::awaitable<void> produce(BoundedQueue<std::uint64_t>* queue) {
  for (std::uint64_t i = 0; i < kBatch; ++i) {
    co_await queue->push(i);
  }
}

asio::awaitable<void> consume(BoundedQueue<std::uint64_t>* queue) {
  for (int i = 0; i < kBatch; ++i) {
    benchmark::DoNotOptimize(co_await queue->pop());
  }
}

void bounded_queue_handoff(benchmark::State& state) {
  asio::io_context io;
  BoundedQueue<std::uint64_t> queue(64);
  for ([[maybe_unused]] auto iteration : state) {
    asio::co_spawn(io, produce(&queue), asio::detached);
    asio::co_spawn(io, consume(&queue), asio::detached);
    io.run();
    io.restart();
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}

asio::awaitable<void> deadline_batch(SystemClock* clock) {
  for (int i = 0; i < kBatch; ++i) {
    benchmark::DoNotOptimize(co_await with_deadline(clock, std::chrono::seconds(10), immediate()));
  }
}

void deadline_wrapper(benchmark::State& state) {
  asio::io_context io;
  SystemClock clock;
  for ([[maybe_unused]] auto iteration : state) {
    asio::co_spawn(io, deadline_batch(&clock), asio::detached);
    io.run();
    io.restart();
  }
  state.SetItemsProcessed(state.iterations() * kBatch);
}

asio::awaitable<void> pool_batch(BlockingPool* pool) {
  for (int i = 0; i < kBatch / 10; ++i) {
    benchmark::DoNotOptimize(co_await pool->run([] { return 1; }));
  }
}

void blocking_pool_round_trip(benchmark::State& state) {
  BlockingPool pool(2, 16);
  asio::io_context io;
  for ([[maybe_unused]] auto iteration : state) {
    asio::co_spawn(io, pool_batch(&pool), asio::detached);
    io.run();
    io.restart();
  }
  state.SetItemsProcessed(state.iterations() * (kBatch / 10));
}

}  // namespace

BENCHMARK(coroutine_resume);
BENCHMARK(cross_shard_round_trip)->UseRealTime();
BENCHMARK(credits_grant_acquire);
BENCHMARK(bounded_queue_handoff);
BENCHMARK(deadline_wrapper);
BENCHMARK(blocking_pool_round_trip)->UseRealTime();
