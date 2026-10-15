#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <clickhouse/block.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/string.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "../src/detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"

namespace {
namespace ch = psim::platform::clickhouse;
namespace asio = boost::asio;

void check(psim::platform::Result<void> result) {
  if (!result) {
    throw std::runtime_error(result.error().message());
  }
}

asio::awaitable<int> measure(std::size_t count, std::size_t threshold) {
  const char* endpoint =
      std::getenv("PSIM_TEST_CLICKHOUSE");  // NOLINT(concurrency-mt-unsafe): fixed before starting the coroutine
  if (endpoint == nullptr) {
    throw std::runtime_error("PSIM_TEST_CLICKHOUSE is required");
  }
  psim::platform::async::BlockingPool blocking(2, 8);
  ch::Pool pool(&blocking, {.endpoints = {{endpoint, 9440}},
                            .password_file = "/repo/.cache/clickhouse-tls/admin.password",
                            .tls = {.ca_file = "/repo/.cache/clickhouse-tls/ca.pem",
                                    .certificate_file = "/repo/.cache/clickhouse-tls/client.pem",
                                    .private_key_file = "/repo/.cache/clickhouse-tls/client.key"},
                            .max_result_rows = count + 1,
                            .max_result_bytes = std::size_t{128} * 1024 * 1024});
  const auto table =
      "chbench_"
      + ch::detail::fingerprint(std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
            .substr(0, 16);
  // psim-arch: allow-sql (generated hexadecimal test table name only)
  check(
      co_await pool.execute_ddl(
          // psim-arch: allow-sql (generated hex identifier only)
          "CREATE TABLE " + table
          + " ON CLUSTER psim_clickhouse (tenant_id String, "
            "event_id String, occurred_at DateTime64(3), value String) ENGINE=ReplicatedReplacingMergeTree("
            "'/clickhouse/ch-component/{shard}/{database}/"
          + table + "', '{replica}') ORDER BY (tenant_id,occurred_at,event_id)"));
  constexpr std::size_t kBatch = 10000;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t offset = 0; offset < count; offset += kBatch) {
    auto tenants = std::make_shared<::clickhouse::ColumnString>();
    auto ids = std::make_shared<::clickhouse::ColumnString>();
    auto times = std::make_shared<::clickhouse::ColumnDateTime64>(3);
    auto values = std::make_shared<::clickhouse::ColumnString>();
    for (std::size_t index = offset; index < std::min(count, offset + kBatch); ++index) {
      tenants->Append("benchmark");
      ids->Append(std::to_string(index));
      times->Append(1000);
      values->Append(std::string(256, 'p'));
    }
    ::clickhouse::Block block;
    block.AppendColumn("tenant_id", tenants);
    block.AppendColumn("event_id", ids);
    block.AppendColumn("occurred_at", times);
    block.AppendColumn("value", values);
    check(co_await pool.insert(table, block, ch::detail::fingerprint(table + "/" + std::to_string(offset))));
  }
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  const auto rate = static_cast<double>(count) / seconds;
  const auto result = co_await pool.select({.table = table,
                                            .columns = {"event_id"},
                                            .tenant = "benchmark",
                                            .from_ms = 0,
                                            .to_ms = 2000,
                                            .limit = count,
                                            .final = false});
  // psim-arch: allow-sql (generated hexadecimal test table name only)
  check(co_await pool.execute_ddl("DROP TABLE " + table + " ON CLUSTER psim_clickhouse SYNC"));
  if (!result || result->rows != count) {
    throw std::runtime_error("ClickHouse benchmark row reconciliation failed");
  }
  std::cout << "ClickHouse native TLS: " << count << " rows, " << seconds << " s, " << rate << " rows/s (threshold "
            << threshold << ")\n";
  co_return rate >= static_cast<double>(threshold) ? 0 : 1;
}

std::size_t number(const char* text) {
  const std::string_view value(text);
  std::size_t result = 0;
  const auto parsed = std::from_chars(value.begin(), value.end(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.end() || result == 0 || result > 10000000) {
    throw std::runtime_error("invalid positive row count or threshold");
  }
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    const auto count = argc > 1 ? number(arguments[1]) : 1000000;
    const auto threshold = argc > 2 ? number(arguments[2]) : 100000;
    asio::io_context context;
    auto result = asio::co_spawn(context, measure(count, threshold), asio::use_future);
    context.run();
    return result.get();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
