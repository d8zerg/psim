#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

#include "psim/platform/async/credits.hpp"
#include "psim/platform/redis/connection.hpp"

namespace psim::platform::redis {
struct PoolOptions {
  ConnectionOptions connection{};
  std::size_t size = 8;
  Duration acquire_timeout = std::chrono::seconds{5};
};

/// Bounded lazy pool per shard. ROLE is verified on checkout; Sentinel is queried after loss.
/// Waiters are FIFO with deadlines. Leases can safely outlive the closed pool.
class Pool {
 private:
  struct State;

 public:
  class Lease {
   public:
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&& other) noexcept;
    Lease& operator=(Lease&& other) noexcept;
    ~Lease();

    [[nodiscard]] Connection* operator->() const noexcept { return connection_.get(); }

    [[nodiscard]] Connection& operator*() const noexcept { return *connection_; }

   private:
    friend class Pool;
    Lease(std::shared_ptr<State> state, std::unique_ptr<Connection> connection, async::Semaphore::Permit permit);
    void release() noexcept;
    std::shared_ptr<State> state_;
    std::unique_ptr<Connection> connection_;
    async::Semaphore::Permit permit_;
  };

  explicit Pool(PoolOptions options);
  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;
  Pool(Pool&&) = delete;
  Pool& operator=(Pool&&) = delete;
  ~Pool();
  asio::awaitable<Result<Lease>> acquire(Duration timeout = Duration::zero());
  void close();
  [[nodiscard]] std::size_t idle() const noexcept;

 private:
  std::shared_ptr<State> state_;
};
}  // namespace psim::platform::redis
