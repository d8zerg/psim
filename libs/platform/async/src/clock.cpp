#include "psim/platform/async/clock.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <tuple>

namespace psim::platform::async {

namespace {

constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);

}  // namespace

asio::awaitable<bool> SystemClock::sleep_until(SteadyTime deadline) {
  asio::steady_timer timer(co_await asio::this_coro::executor, deadline);
  auto [ec] = co_await timer.async_wait(kAwait);
  co_return !ec;
}

struct ManualClock::Sleeper {
  SteadyTime deadline;
  std::uint64_t sequence;
  asio::steady_timer* event;
  bool woken = false;
};

asio::awaitable<bool> ManualClock::sleep_until(SteadyTime deadline) {
  if (deadline <= steady_) {
    co_return true;
  }
  // The event timer never expires by itself: advance() cancels it when the deadline is reached.
  asio::steady_timer event(co_await asio::this_coro::executor, asio::steady_timer::time_point::max());
  Sleeper sleeper{.deadline = deadline, .sequence = sequence_++, .event = &event};
  const auto position = std::ranges::upper_bound(sleepers_, std::tie(deadline, sleeper.sequence), {},
                                                 [](const Sleeper* s) { return std::tie(s->deadline, s->sequence); });
  const auto it = sleepers_.insert(position, &sleeper);
  std::ignore = co_await event.async_wait(kAwait);
  if (!sleeper.woken) {
    sleepers_.erase(it);  // cancelled from outside before the deadline
  }
  co_return sleeper.woken;
}

void ManualClock::advance(Duration duration) {
  steady_ += duration;
  wall_ += std::chrono::duration_cast<WallTime::duration>(duration);
  while (!sleepers_.empty() && sleepers_.front()->deadline <= steady_) {
    Sleeper* sleeper = sleepers_.front();
    sleepers_.pop_front();
    sleeper->woken = true;
    sleeper->event->cancel();
  }
}

bool ManualClock::advance_to_next() {
  if (sleepers_.empty()) {
    return false;
  }
  advance(sleepers_.front()->deadline - steady_);
  return true;
}

}  // namespace psim::platform::async
