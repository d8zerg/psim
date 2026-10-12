#pragma once

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/redis/connection.hpp>
#include <boost/redis/response.hpp>
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

#include "psim/platform/redis/connection.hpp"

namespace psim::platform::redis::testing {
template <typename T>
T run(asio::awaitable<T> operation) {
  asio::io_context io;
  auto result = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.run();
  return result.get();
}

template <typename T>
T take(Result<T> result) {
  if (!result) {
    ADD_FAILURE() << result.error().message();
    throw std::runtime_error(result.error().message());
  }
  return std::move(*result);
}

inline void check(Result<void> result) {
  if (!result) {
    ADD_FAILURE() << result.error().message();
    throw std::runtime_error(result.error().message());
  }
}

inline std::string environment(const char* name) {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): endpoints are set before test threads
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string("component endpoint not set: ") + name);
  }
  return value;
}

inline ConnectionOptions options(bool fault = false) {
  ConnectionOptions config;
  config.endpoint = {environment(fault ? "PSIM_TEST_REDIS_FAULT" : "PSIM_TEST_REDIS"), fault ? "16379" : "6379"};
  config.credentials.password = "psim-dev-valkey";
  config.tls.enabled = false;
  return config;
}

inline ConnectionOptions sentinel_options() {
  auto config = options();
  config.sentinel.credentials.password = "psim-dev-sentinel";
  config.sentinel.tls.enabled = false;
  std::string hosts = environment("PSIM_TEST_SENTINELS");
  while (!hosts.empty()) {
    const auto comma = hosts.find(',');
    config.sentinel.endpoints.push_back({hosts.substr(0, comma), "26379"});
    if (comma == std::string::npos) {
      break;
    }
    hosts.erase(0, comma + 1);
  }
  return config;
}

inline std::string unique() {
  static std::mt19937_64 random{std::random_device{}()};
  return "redistest_" + std::to_string(random());
}

/// Raw RESP3 client for test-only administration and notification reception.
class Admin {
  struct State {
    explicit State(asio::any_io_executor executor)
        : connection(std::move(executor), boost::redis::logger{boost::redis::logger::level::disabled}) {}

    boost::redis::connection connection;
    boost::redis::config config;
  };

 public:
  Admin(asio::any_io_executor executor, Endpoint endpoint, std::string password = "psim-dev-valkey")
      : state_(std::make_shared<State>(std::move(executor))) {
    state_->config.addr = {std::move(endpoint.host), std::move(endpoint.port)};
    state_->config.password = std::move(password);
    state_->config.reconnect_wait_interval = Duration::zero();
    state_->config.health_check_interval = Duration::zero();
    state_->connection.async_run(state_->config, [state = state_](boost::system::error_code) {});
  }

  Admin(const Admin&) = delete;
  Admin& operator=(const Admin&) = delete;
  Admin(Admin&&) = delete;
  Admin& operator=(Admin&&) = delete;

  ~Admin() { state_->connection.cancel(); }

  boost::redis::connection& native() { return state_->connection; }

  asio::awaitable<boost::redis::generic_response> execute(Command command) {
    boost::redis::request request;
    if (command.size() == 1) {
      request.push(command[0]);
    } else if (command.size() == 2) {
      request.push(command[0], command[1]);
    } else {
      request.push_range(command[0], command[1], command.begin() + 2, command.end());
    }
    boost::redis::generic_response reply;
    co_await state_->connection.async_exec(request, reply, asio::use_awaitable);
    co_return reply;
  }

 private:
  std::shared_ptr<State> state_;
};

inline void proxy_enable(bool enabled) {
  namespace http = boost::beast::http;
  const auto api = environment("PSIM_TEST_TOXIPROXY");
  const auto colon = api.rfind(':');
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket socket(io);
  asio::connect(socket, resolver.resolve(api.substr(0, colon), api.substr(colon + 1)));
  http::request<http::string_body> request(http::verb::post, "/proxies/valkey", 11);
  request.set(http::field::host, api);
  request.set(http::field::content_type, "application/json");
  request.body() = enabled ? R"({"enabled": true})" : R"({"enabled": false})";
  request.prepare_payload();
  http::write(socket, request);
  boost::beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  if (response.result_int() != 200) {
    throw std::runtime_error("Toxiproxy could not update Valkey proxy");
  }
}
}  // namespace psim::platform::redis::testing
