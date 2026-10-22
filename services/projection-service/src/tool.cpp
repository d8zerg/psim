#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/pg/pool.hpp"
#include "rebuild.hpp"

namespace {
std::string env(const char* name, std::string fallback = "") {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): configuration is read before threads start.
  return value ? std::string(value) : std::move(fallback);
}

std::int64_t integer(std::string_view text) {
  std::int64_t value = 0;
  const auto parsed = std::from_chars(text.begin(), text.end(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.end()) {
    throw std::invalid_argument("invalid integer argument");
  }
  return value;
}
}  // namespace

int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  if (argc != 5 || std::string_view(args[1]) != "rebuild") {
    std::cerr << "usage: psim-report-tool rebuild TENANT FROM_MS TO_MS\n";
    return 2;
  }
  try {
    const auto from = integer(args[3]);
    const auto to = integer(args[4]);
    psim::platform::pg::PoolOptions pg;
    pg.connection.host = env("PSIM_PG_HOST", "postgres");
    pg.connection.port = env("PSIM_PG_PORT", "5432");
    pg.connection.database = env("PSIM_PG_DATABASE", "psim");
    pg.connection.user = env("PSIM_PG_USER", "psim");
    pg.connection.password = env("PSIM_PG_PASSWORD");
    pg.connection.properties = {{"sslmode", env("PSIM_PG_SSLMODE", "verify-full")},
                                {"sslrootcert", env("PSIM_PG_SSLROOTCERT")},
                                {"sslcert", env("PSIM_PG_SSLCERT")},
                                {"sslkey", env("PSIM_PG_SSLKEY")}};
    psim::platform::clickhouse::PoolOptions ch;
    const auto port = integer(env("PSIM_CH_PORT", "9440"));
    if (port < 1 || port > 65535) {
      throw std::invalid_argument("invalid ClickHouse port");
    }
    ch.endpoints = {{env("PSIM_CH_HOST", "clickhouse"), static_cast<std::uint16_t>(port)}};
    ch.user = env("PSIM_CH_USER", "psim");
    ch.password_file = env("PSIM_CH_PASSWORD_FILE");
    ch.tls = {.ca_file = env("PSIM_CH_CA_FILE"),
              .certificate_file = env("PSIM_CH_CERT_FILE"),
              .private_key_file = env("PSIM_CH_KEY_FILE")};
    ch.allow_plaintext = env("PSIM_CH_ALLOW_PLAINTEXT") == "1";
    boost::asio::io_context context;
    psim::platform::async::BlockingPool blocking(4, 32);
    psim::platform::pg::Pool source(std::move(pg));
    psim::platform::clickhouse::Pool destination(&blocking, std::move(ch));
    auto result = boost::asio::co_spawn(context, psim::projection::rebuild(&source, &destination, args[2], from, to),
                                        boost::asio::use_future);
    context.run();
    auto completed = result.get();
    if (!completed) {
      std::cerr << completed.error().message() << '\n';
      return 1;
    }
    std::cout << *completed << " incident snapshots rebuilt\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
