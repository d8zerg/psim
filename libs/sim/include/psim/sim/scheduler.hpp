#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <exception>

#include "psim/platform/async/clock.hpp"

namespace psim::sim {

struct RunBudget {
  std::size_t handlers = 100000;
  platform::async::Duration virtual_time = std::chrono::seconds{30};
};

/// A single executor driven only by ManualClock. No threads, network or real-time waits.
/// Own the actors and their dependencies for at least as long as this scheduler.
class Scheduler {
 public:
  void spawn(boost::asio::awaitable<void> actor);
  /// Throws on actor failure, deadlock, or budget exhaustion, rather than hanging a manual run.
  void run(RunBudget budget = {});

  [[nodiscard]] platform::async::ManualClock& clock() noexcept { return clock_; }

  [[nodiscard]] std::size_t handlers() const noexcept { return handlers_; }

 private:
  // io_context must destroy coroutine frames before their clock is destroyed.
  platform::async::ManualClock clock_;
  boost::asio::io_context io_{1};
  std::size_t active_ = 0;
  std::size_t handlers_ = 0;
  std::exception_ptr error_;
};

}  // namespace psim::sim
