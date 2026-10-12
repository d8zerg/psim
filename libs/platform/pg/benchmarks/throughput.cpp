#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>
#include <ratio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/pg/connection.hpp"

namespace {
namespace pg = psim::platform::pg;
namespace asio = boost::asio;

template <typename T>
T take(psim::platform::Result<T> result) {
  if (!result) {
    throw std::runtime_error(result.error().message());
  }
  return std::move(*result);
}

std::string endpoint() {
  const char* host = std::getenv("PSIM_TEST_POSTGRES");  // NOLINT(concurrency-mt-unsafe): startup only
  if (!host) {
    throw std::runtime_error("run PostgreSQL benchmark through task bench:pg against the environment");
  }
  return host;
}

asio::awaitable<void> benchmark() {
  auto connection = take(co_await pg::Connection::connect(
      {.host = endpoint(), .password = "psim-dev-psim", .properties = {{"sslmode", "disable"}}}));
  take(co_await connection->query({.sql = "CREATE TEMPORARY TABLE pg_bench (id bigint PRIMARY KEY, value text)"}));
  constexpr int kRows = 20000;
  std::vector<std::string> chunks;
  for (int first = 0; first < kRows; first += 1000) {
    std::string chunk;
    for (int row = first; row < first + 1000; ++row) {
      chunk += std::to_string(row) + "\tpayload\n";
    }
    chunks.push_back(std::move(chunk));
  }
  auto start = std::chrono::steady_clock::now();
  const auto copied = co_await connection->copy_in("COPY pg_bench (id, value) FROM STDIN", std::move(chunks));
  if (!copied) {
    throw std::runtime_error(copied.error().message());
  }
  const double copy_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::println("COPY: {:.0f} rows/s ({} rows)", static_cast<double>(kRows) / copy_seconds, kRows);
  start = std::chrono::steady_clock::now();
  for (int first = kRows; first < 2 * kRows; first += 100) {
    std::vector<pg::Query> queries;
    for (int row = first; row < first + 100; ++row) {
      queries.push_back({.sql = "INSERT INTO pg_bench (id, value) VALUES ($1, $2)",
                         .parameters = {pg::Parameter::integer(row), pg::Parameter::text("payload")}});
    }
    take(co_await connection->pipeline(std::move(queries)));
  }
  const double pipeline_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::println("Pipeline: {:.0f} rows/s ({} rows, 100 queries/batch)", static_cast<double>(kRows) / pipeline_seconds,
               kRows);
  std::vector<double> latency;
  for (int query = 0; query < 200; ++query) {
    start = std::chrono::steady_clock::now();
    take(co_await connection->query({.sql = "SELECT 42"}));
    latency.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
  }
  std::ranges::sort(latency);
  std::println("Query latency: p50 {:.3f} ms, p99 {:.3f} ms", latency[100], latency[198]);
  const auto total = take(co_await connection->query({.sql = "SELECT count(*) FROM pg_bench"}));
  if (take(total.integer(0, 0)) != std::int64_t{2} * kRows) {
    throw std::runtime_error("PostgreSQL benchmark lost rows");
  }
}
}  // namespace

int main() {
  try {
    asio::io_context io;
    auto future = asio::co_spawn(io, benchmark(), asio::use_future);
    io.run();
    future.get();
    return 0;
  } catch (const std::exception& error) {
    (void)std::fputs(error.what(), stderr);
    (void)std::fputs("\n", stderr);
    return 1;
  }
}
