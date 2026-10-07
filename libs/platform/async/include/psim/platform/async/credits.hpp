#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cstddef>
#include <cstdint>
#include <list>

namespace psim::platform::async {

namespace asio = boost::asio;

/// Credit-based flow control on one executor (ADR-016, SR-18): a consumer grants credits in batches,
/// a producer takes as many as it needs and waits otherwise. Requests are served strictly in
/// order, so a large request is not starved by small ones. close() fails every waiter.
class Credits {
 public:
  explicit Credits(std::uint64_t initial = 0) : available_(initial) {}

  void grant(std::uint64_t credits);

  [[nodiscard]] bool try_acquire(std::uint64_t credits);

  /// Wait until `credits` are available and take them; false when closed or cancelled.
  asio::awaitable<bool> acquire(std::uint64_t credits);

  void close();

  [[nodiscard]] std::uint64_t available() const noexcept { return available_; }

  [[nodiscard]] std::size_t waiting() const noexcept { return waiters_.size(); }

  [[nodiscard]] bool closed() const noexcept { return closed_; }

 private:
  struct Waiter {
    std::uint64_t credits = 0;
    asio::steady_timer* event = nullptr;
    bool granted = false;
  };

  void serve();

  std::uint64_t available_;
  bool closed_ = false;
  std::list<Waiter*> waiters_;  // in waiting order
};

/// Counting semaphore on one executor; a Permit returns its unit when destroyed.
class Semaphore {
 public:
  explicit Semaphore(std::uint64_t permits) : credits_(permits) {}

  class Permit {
   public:
    Permit() = default;

    explicit Permit(Semaphore* owner) : owner_(owner) {}

    Permit(const Permit&) = delete;
    Permit& operator=(const Permit&) = delete;

    Permit(Permit&& other) noexcept : owner_(other.owner_) { other.owner_ = nullptr; }

    Permit& operator=(Permit&& other) noexcept {
      if (this != &other) {
        release();
        owner_ = other.owner_;
        other.owner_ = nullptr;
      }
      return *this;
    }

    ~Permit() { release(); }

    [[nodiscard]] explicit operator bool() const noexcept { return owner_ != nullptr; }

    void release() noexcept {
      if (owner_ != nullptr) {
        Semaphore* owner = owner_;
        owner_ = nullptr;
        try {
          owner->credits_.grant(1);
        } catch (...) {  // NOLINT(bugprone-empty-catch): waking a waiter cancels a timer, which cannot fail here
        }
      }
    }

   private:
    Semaphore* owner_ = nullptr;
  };

  /// An empty Permit when closed or cancelled.
  asio::awaitable<Permit> acquire();

  [[nodiscard]] Permit try_acquire() { return credits_.try_acquire(1) ? Permit(this) : Permit(); }

  void close() { credits_.close(); }

  [[nodiscard]] std::uint64_t available() const noexcept { return credits_.available(); }

 private:
  Credits credits_;
};

}  // namespace psim::platform::async
