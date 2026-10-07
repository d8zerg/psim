#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>

namespace psim::platform::async {

namespace asio = boost::asio;

using WallTime = std::chrono::system_clock::time_point;
using SteadyTime = std::chrono::steady_clock::time_point;
using Duration = std::chrono::nanoseconds;

/// Time port (ADR-016): event time, monotonic time and waiting. Code that waits or measures
/// through Clock runs deterministically under ManualClock (tests, simulation of step 2.7).
class Clock {
 public:
  Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  Clock(Clock&&) = delete;
  Clock& operator=(Clock&&) = delete;
  virtual ~Clock() = default;

  /// Wall time for timestamps of events.
  [[nodiscard]] virtual WallTime wall() const = 0;
  /// Monotonic time for durations and deadlines.
  [[nodiscard]] virtual SteadyTime steady() const = 0;
  /// Resume at `deadline` (true) or earlier when the operation is cancelled (false). Runs on the
  /// executor of the calling coroutine.
  virtual asio::awaitable<bool> sleep_until(SteadyTime deadline) = 0;

  asio::awaitable<bool> sleep_for(Duration duration) { return sleep_until(steady() + duration); }
};

/// Real time: system clocks and Asio steady timers.
class SystemClock final : public Clock {
 public:
  [[nodiscard]] WallTime wall() const override { return std::chrono::system_clock::now(); }

  [[nodiscard]] SteadyTime steady() const override { return std::chrono::steady_clock::now(); }

  asio::awaitable<bool> sleep_until(SteadyTime deadline) override;
};

/// Time that moves only when advance() is called. Sleepers wake in deadline order (ties in the
/// order they went to sleep), each on its own executor, so runs are reproducible. Not thread-safe:
/// use it from the thread that runs the io_context of the sleepers.
class ManualClock final : public Clock {
 public:
  explicit ManualClock(WallTime wall_start = WallTime{}, SteadyTime steady_start = SteadyTime{})
      : wall_(wall_start), steady_(steady_start) {}

  [[nodiscard]] WallTime wall() const override { return wall_; }

  [[nodiscard]] SteadyTime steady() const override { return steady_; }

  asio::awaitable<bool> sleep_until(SteadyTime deadline) override;

  /// Move time forward and wake every sleeper whose deadline is reached.
  void advance(Duration duration);
  /// Move to the earliest deadline of the sleepers, if any; returns whether time moved.
  bool advance_to_next();

  [[nodiscard]] std::size_t sleepers() const noexcept { return sleepers_.size(); }

 private:
  struct Sleeper;

  WallTime wall_;
  SteadyTime steady_;
  std::uint64_t sequence_ = 0;
  std::list<Sleeper*> sleepers_;
};

}  // namespace psim::platform::async
