#pragma once

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

#include "psim/platform/async/detail/wait_list.hpp"

namespace psim::platform::async {

namespace asio = boost::asio;

/// Bounded FIFO queue between stages on one executor (ADR-016, SR-18): a producer waits for room,
/// a consumer waits for an item. close() stops new items; the items already queued are still
/// delivered, so a stage drains its input before it stops.
template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  /// Wait for room and enqueue; false when the queue is closed or the wait is cancelled.
  asio::awaitable<bool> push(T value) {
    while (!closed_ && items_.size() >= capacity_) {
      if (!co_await not_full_.wait()) {
        co_return false;
      }
    }
    if (closed_) {
      co_return false;
    }
    items_.push_back(std::move(value));
    not_empty_.notify_one();
    co_return true;
  }

  /// Enqueue without waiting; false when full or closed (the caller decides: drop, retry, reject).
  [[nodiscard]] bool try_push(T value) {
    if (closed_ || items_.size() >= capacity_) {
      return false;
    }
    items_.push_back(std::move(value));
    not_empty_.notify_one();
    return true;
  }

  /// Next item; nullopt when the queue is closed and empty, or when the wait is cancelled.
  asio::awaitable<std::optional<T>> pop() {
    while (items_.empty()) {
      if (closed_ || !co_await not_empty_.wait()) {
        co_return std::nullopt;
      }
    }
    std::optional<T> item(std::move(items_.front()));
    items_.pop_front();
    not_full_.notify_one();
    co_return item;
  }

  void close() {
    closed_ = true;
    not_full_.notify_all();
    not_empty_.notify_all();
  }

  [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

  [[nodiscard]] bool closed() const noexcept { return closed_; }

 private:
  std::size_t capacity_;
  bool closed_ = false;
  std::deque<T> items_;
  detail::WaitList not_full_;
  detail::WaitList not_empty_;
};

}  // namespace psim::platform::async
