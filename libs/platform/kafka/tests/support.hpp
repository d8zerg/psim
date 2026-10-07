#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <utility>

namespace psim::platform::kafka::testing {

namespace asio = boost::asio;

/// Run a coroutine to completion on a fresh io_context.
template <typename T>
T run(asio::awaitable<T> operation) {
  asio::io_context io;
  auto result = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.run();
  return result.get();
}

}  // namespace psim::platform::kafka::testing
