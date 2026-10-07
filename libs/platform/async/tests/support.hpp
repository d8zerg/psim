#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace psim::platform::async::testing {

namespace asio = boost::asio;

/// Start a coroutine on `io`; its result lands in the returned holder once io has run it.
template <typename T>
class Spawned {
 public:
  Spawned(asio::io_context& io, asio::awaitable<T> operation) {
    asio::co_spawn(io, std::move(operation), [this](const std::exception_ptr& error, T value) {
      if (error) {
        std::rethrow_exception(error);
      }
      result_ = std::move(value);
    });
  }

  Spawned(const Spawned&) = delete;
  Spawned& operator=(const Spawned&) = delete;
  Spawned(Spawned&&) = delete;
  Spawned& operator=(Spawned&&) = delete;
  ~Spawned() = default;

  [[nodiscard]] bool done() const noexcept { return result_.has_value(); }

  [[nodiscard]] const T& value() const {
    if (!result_) {
      throw std::logic_error("the coroutine has not finished");
    }
    return *result_;
  }

 private:
  std::optional<T> result_;
};

/// Run a coroutine to completion on a fresh io_context.
template <typename T>
T run(asio::awaitable<T> operation) {
  asio::io_context io;
  auto result = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.run();
  return result.get();
}

}  // namespace psim::platform::async::testing
