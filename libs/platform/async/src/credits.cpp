#include "psim/platform/async/credits.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <cstdint>
#include <tuple>

namespace psim::platform::async {

void Credits::grant(std::uint64_t credits) {
  available_ += credits;
  serve();
}

bool Credits::try_acquire(std::uint64_t credits) {
  if (closed_ || !waiters_.empty() || available_ < credits) {
    return false;
  }
  available_ -= credits;
  return true;
}

asio::awaitable<bool> Credits::acquire(std::uint64_t credits) {
  if (try_acquire(credits)) {
    co_return true;
  }
  if (closed_) {
    co_return false;
  }
  asio::steady_timer event(co_await asio::this_coro::executor, asio::steady_timer::time_point::max());
  Waiter waiter{.credits = credits, .event = &event};
  const auto it = waiters_.insert(waiters_.end(), &waiter);
  std::ignore = co_await event.async_wait(asio::as_tuple(asio::use_awaitable));
  if (waiter.granted) {
    co_return true;  // serve() took the credits for this waiter before waking it
  }
  if (!closed_) {
    waiters_.erase(it);  // cancelled from outside; requests behind it may now be served
    serve();
  }
  co_return false;
}

void Credits::serve() {
  while (!closed_ && !waiters_.empty() && available_ >= waiters_.front()->credits) {
    Waiter* waiter = waiters_.front();
    waiters_.pop_front();
    available_ -= waiter->credits;
    waiter->granted = true;
    waiter->event->cancel();
  }
}

void Credits::close() {
  closed_ = true;
  for (Waiter* waiter : waiters_) {
    waiter->event->cancel();
  }
  waiters_.clear();
}

asio::awaitable<Semaphore::Permit> Semaphore::acquire() {
  if (co_await credits_.acquire(1)) {
    co_return Permit(this);
  }
  co_return Permit();
}

}  // namespace psim::platform::async
