#include "psim/sim/scheduler.hpp"

#include <boost/asio/co_spawn.hpp>

#include <exception>
#include <stdexcept>
#include <utility>

#include "psim/platform/async/clock.hpp"

namespace psim::sim {

void Scheduler::spawn(boost::asio::awaitable<void> actor) {
  ++active_;
  boost::asio::co_spawn(io_, std::move(actor), [this](const std::exception_ptr& error) {
    --active_;
    if (error && !error_) {
      error_ = error;
    }
  });
}

void Scheduler::run(RunBudget budget) {
  if (budget.handlers == 0 || budget.virtual_time < platform::async::Duration::zero()) {
    throw std::invalid_argument("invalid simulation budget");
  }
  const auto started = clock_.steady();
  handlers_ = 0;
  while (active_ != 0) {
    if (handlers_ >= budget.handlers || clock_.steady() - started > budget.virtual_time) {
      throw std::runtime_error("simulation budget exhausted");
    }
    io_.restart();
    if (io_.poll_one() != 0) {
      ++handlers_;
    } else if (!clock_.advance_to_next()) {
      throw std::runtime_error("simulation deadlock: actors have no runnable work or virtual timers");
    }
    if (error_) {
      std::rethrow_exception(error_);
    }
  }
}

}  // namespace psim::sim
