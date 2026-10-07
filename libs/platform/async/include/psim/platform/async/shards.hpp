#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace psim::platform::async {

namespace asio = boost::asio;

/// Number of CPUs the process may run on (affinity mask), at least 1.
[[nodiscard]] std::size_t available_cpus() noexcept;

/// Thread per core (ADR-016): every shard is a thread with its own io_context. State bound to a
/// shard (a Kafka partition, its store, a connection) is touched only by that thread; shards talk
/// by posting work to each other.
class Shards {
 public:
  /// `count` shards; 0 means available_cpus().
  explicit Shards(std::size_t count, std::string name = "psim-shard");
  Shards(const Shards&) = delete;
  Shards& operator=(const Shards&) = delete;
  Shards(Shards&&) = delete;
  Shards& operator=(Shards&&) = delete;
  ~Shards();

  [[nodiscard]] std::size_t size() const noexcept { return contexts_.size(); }

  [[nodiscard]] asio::any_io_executor executor(std::size_t index) const;

  /// Stable shard of a key, for example the hash of a partition or of a correlation key.
  [[nodiscard]] std::size_t index_for(std::uint64_t key) const noexcept { return key % size(); }

  template <typename Function>
  void post(std::size_t index, Function&& function) const {
    asio::post(executor(index), std::forward<Function>(function));
  }

  /// Start the threads; idempotent.
  void start();
  /// Let the shards finish their queued work and join the threads; a shard still busy after
  /// `grace` (a component left a timer behind) is stopped. Call after the components drained.
  void stop(std::chrono::milliseconds grace = std::chrono::milliseconds(5000));

 private:
  std::string name_;
  std::vector<std::unique_ptr<asio::io_context>> contexts_;
  std::vector<std::optional<asio::executor_work_guard<asio::io_context::executor_type>>> guards_;
  std::vector<std::thread> threads_;
};

}  // namespace psim::platform::async
