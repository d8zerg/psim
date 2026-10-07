#pragma once

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <cstddef>
#include <list>
#include <tuple>

namespace psim::platform::async::detail {

namespace asio = boost::asio;

/// FIFO list of coroutines waiting on one executor. wait() resumes with true when notified, with
/// false when the wait is cancelled from outside (the waiter then leaves the list).
class WaitList {
 public:
  asio::awaitable<bool> wait() {
    asio::steady_timer event(co_await asio::this_coro::executor, asio::steady_timer::time_point::max());
    Waiter waiter{.event = &event};
    const auto it = waiters_.insert(waiters_.end(), &waiter);
    std::ignore = co_await event.async_wait(asio::as_tuple(asio::use_awaitable));
    if (!waiter.notified) {
      waiters_.erase(it);
    }
    co_return waiter.notified;
  }

  /// Wake the longest waiting coroutine; false when nobody waits.
  bool notify_one() {
    if (waiters_.empty()) {
      return false;
    }
    Waiter* waiter = waiters_.front();
    waiters_.pop_front();
    waiter->notified = true;
    waiter->event->cancel();
    return true;
  }

  void notify_all() {
    while (notify_one()) {
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return waiters_.size(); }

  [[nodiscard]] bool empty() const noexcept { return waiters_.empty(); }

 private:
  struct Waiter {
    asio::steady_timer* event;
    bool notified = false;
  };

  std::list<Waiter*> waiters_;
};

}  // namespace psim::platform::async::detail
