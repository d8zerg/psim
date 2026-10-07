#include "psim/platform/async/credits.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/async/deadline.hpp"
#include "support.hpp"

namespace {

namespace asio = boost::asio;
using psim::platform::async::Credits;
using psim::platform::async::Semaphore;
using psim::platform::async::spawn_cancellable;
using psim::platform::async::testing::Spawned;

asio::awaitable<bool> take(Credits* credits, std::uint64_t amount, std::vector<std::string>* log, std::string name) {
  const bool granted = co_await credits->acquire(amount);
  log->push_back(name + (granted ? ":granted" : ":refused"));
  co_return granted;
}

TEST(Credits, ServeRequestsStrictlyInOrder) {
  asio::io_context io;
  Credits credits;
  std::vector<std::string> log;
  const Spawned<bool> big(io, take(&credits, 5, &log, "big"));
  const Spawned<bool> small(io, take(&credits, 1, &log, "small"));
  io.poll();
  EXPECT_EQ(credits.waiting(), 2U);

  credits.grant(1);  // enough for "small", but "big" is first: no starvation of large requests
  io.poll();
  EXPECT_TRUE(log.empty());
  EXPECT_FALSE(credits.try_acquire(1));  // nobody jumps the queue

  credits.grant(4);
  io.poll();
  EXPECT_EQ(log, std::vector<std::string>{"big:granted"});

  credits.grant(1);
  io.poll();
  EXPECT_EQ(log, (std::vector<std::string>{"big:granted", "small:granted"}));
  EXPECT_EQ(credits.available(), 0U);
}

TEST(Credits, ACancelledRequestLetsTheNextOneThrough) {
  asio::io_context io;
  Credits credits;
  std::vector<std::string> log;
  const auto blocked = spawn_cancellable(io.get_executor(), take(&credits, 10, &log, "blocked"));
  const Spawned<bool> next(io, take(&credits, 2, &log, "next"));
  credits.grant(2);
  io.poll();
  EXPECT_TRUE(log.empty());

  blocked.cancel();
  io.poll();

  EXPECT_EQ(log, (std::vector<std::string>{"blocked:refused", "next:granted"}));
  EXPECT_EQ(credits.waiting(), 0U);
}

TEST(Credits, CloseRefusesWaitersAndNewRequests) {
  asio::io_context io;
  Credits credits(3);
  EXPECT_TRUE(credits.try_acquire(3));
  std::vector<std::string> log;
  const Spawned<bool> waiter(io, take(&credits, 1, &log, "waiter"));
  io.poll();

  credits.close();
  io.poll();

  EXPECT_EQ(log, std::vector<std::string>{"waiter:refused"});
  EXPECT_TRUE(credits.closed());
  EXPECT_FALSE(credits.try_acquire(0));
  const Spawned<bool> late(io, take(&credits, 1, &log, "late"));
  io.restart();  // the context stopped when it ran out of work
  io.poll();
  EXPECT_FALSE(late.value());
}

asio::awaitable<bool> hold(Semaphore* semaphore, std::vector<Semaphore::Permit>* held) {
  auto permit = co_await semaphore->acquire();
  const bool granted = static_cast<bool>(permit);
  if (granted) {
    held->push_back(std::move(permit));
  }
  co_return granted;
}

TEST(Semaphore, PermitsReturnTheirUnitWhenReleased) {
  asio::io_context io;
  Semaphore semaphore(2);
  std::vector<Semaphore::Permit> held;
  const Spawned<bool> first(io, hold(&semaphore, &held));
  const Spawned<bool> second(io, hold(&semaphore, &held));
  const Spawned<bool> third(io, hold(&semaphore, &held));
  io.poll();
  EXPECT_EQ(held.size(), 2U);
  EXPECT_FALSE(third.done());
  EXPECT_FALSE(semaphore.try_acquire());

  held.front().release();
  io.poll();
  EXPECT_TRUE(third.value());
  EXPECT_EQ(semaphore.available(), 0U);

  held.clear();
  EXPECT_EQ(semaphore.available(), 2U);
  semaphore.close();
  EXPECT_FALSE(semaphore.try_acquire());
}

TEST(Semaphore, PermitsMoveTheirOwnership) {
  Semaphore semaphore(1);
  auto permit = semaphore.try_acquire();
  ASSERT_TRUE(permit);
  Semaphore::Permit moved(std::move(permit));
  EXPECT_FALSE(
      permit);  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move): the moved-from state is the point
  Semaphore::Permit assigned;
  assigned = std::move(moved);
  EXPECT_EQ(semaphore.available(), 0U);
  assigned = Semaphore::Permit();
  EXPECT_EQ(semaphore.available(), 1U);
}

}  // namespace
