#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

#include "../src/sql.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/pg/connection.hpp"

namespace psim::platform::pg::testing {
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
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): tests configure endpoints before threads
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string("component endpoint not set: ") + name);
  }
  return value;
}

inline ConnectionOptions options(bool fault = false) {
  return {.host = environment(fault ? "PSIM_TEST_POSTGRES_FAULT" : "PSIM_TEST_POSTGRES"),
          .port = fault ? "15432" : "5432",
          .password = "psim-dev-psim",
          .properties = {{"sslmode", "disable"}}};
}

inline std::string unique_schema() {
  static std::mt19937_64 random{std::random_device{}()};
  return "pgtest_" + std::to_string(random());
}

inline std::string sql(const std::string& schema, std::string_view statement) {
  return take(detail::schema_sql(schema, statement));
}

inline asio::awaitable<std::unique_ptr<Connection>> connect(bool fault = false) {
  co_return take(co_await Connection::connect(options(fault)));
}

inline asio::awaitable<void> drop(std::string schema) {
  auto connection = co_await connect();
  take(co_await connection->query({.sql = sql(schema, "DROP SCHEMA IF EXISTS @schema@ CASCADE")}));
}
}  // namespace psim::platform::pg::testing
