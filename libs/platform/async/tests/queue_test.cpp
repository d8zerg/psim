#include "psim/platform/async/queue.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "psim/platform/async/deadline.hpp"
#include "support.hpp"

namespace {

namespace asio = boost::asio;
using psim::platform::async::BoundedQueue;
using psim::platform::async::spawn_cancellable;
using psim::platform::async::testing::Spawned;

asio::awaitable<bool> produce(BoundedQueue<int>* queue, int first, int count) {
  for (int i = first; i < first + count; ++i) {
    if (!co_await queue->push(i)) {
      co_return false;
    }
  }
  co_return true;
}

asio::awaitable<std::vector<int>> consume(BoundedQueue<int>* queue) {
  std::vector<int> items;
  while (auto item = co_await queue->pop()) {
    items.push_back(*item);
  }
  co_return items;
}

TEST(BoundedQueue, AProducerWaitsForRoomAndOrderIsKept) {
  asio::io_context io;
  BoundedQueue<int> queue(2);
  const Spawned<bool> producer(io, produce(&queue, 0, 5));
  io.poll();
  EXPECT_EQ(queue.size(), 2U);  // the producer is held back at the capacity
  EXPECT_FALSE(producer.done());

  const Spawned<std::vector<int>> consumer(io, consume(&queue));
  io.poll();
  EXPECT_TRUE(producer.value());
  queue.close();
  io.poll();

  EXPECT_EQ(consumer.value(), (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(BoundedQueue, CloseRefusesProducersButDeliversQueuedItems) {
  asio::io_context io;
  BoundedQueue<int> queue(1);
  ASSERT_TRUE(queue.try_push(7));
  EXPECT_FALSE(queue.try_push(8));
  const Spawned<bool> blocked(io, produce(&queue, 8, 1));
  io.poll();

  queue.close();
  io.poll();

  EXPECT_FALSE(blocked.value());
  EXPECT_FALSE(queue.try_push(9));
  EXPECT_TRUE(queue.closed());
  const Spawned<std::vector<int>> consumer(io, consume(&queue));
  io.restart();  // the context stopped when it ran out of work
  io.poll();
  EXPECT_EQ(consumer.value(), std::vector<int>{7});
  EXPECT_EQ(queue.capacity(), 1U);
}

TEST(BoundedQueue, ACancelledPopReturnsNothing) {
  asio::io_context io;
  BoundedQueue<int> queue(1);
  bool finished = false;
  auto pop_once = [](BoundedQueue<int>* q, bool* done) -> asio::awaitable<void> {
    EXPECT_FALSE((co_await q->pop()).has_value());
    *done = true;
  };
  const auto handle = spawn_cancellable(io.get_executor(), pop_once(&queue, &finished));
  io.poll();
  handle.cancel();
  io.poll();
  EXPECT_TRUE(finished);
}

}  // namespace
