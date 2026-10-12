#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <fstream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"

namespace {
namespace pg = psim::platform::pg;
namespace asio = boost::asio;

std::string setting(const char* name, std::string fallback = {}) {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): startup before any thread
  return value ? std::string(value) : std::move(fallback);
}

int run(std::span<char*> arguments) {
  if (arguments.size() != 3
      || (std::string_view(arguments[1]) != "expand" && std::string_view(arguments[1]) != "contract")) {
    std::println(stderr, "usage: psim-pg-migrate expand|contract manifest.json (connection: PSIM_PG_* environment)");
    return 2;
  }
  std::ifstream file(arguments[2]);
  if (!file) {
    std::println(stderr, "psim-pg-migrate: manifest cannot be read");
    return 1;
  }
  const auto manifest = nlohmann::json::parse(file);
  std::vector<pg::Migration> migrations;
  for (const auto& item : manifest.at("migrations")) {
    migrations.push_back({.version = item.at("version").get<std::int64_t>(),
                          .name = item.at("name").get<std::string>(),
                          .expand = item.at("expand").get<std::vector<std::string>>(),
                          .contract = item.value("contract", std::vector<std::string>{})});
  }
  pg::ConnectionOptions options{.host = setting("PSIM_PG_HOST", "127.0.0.1"),
                                .port = setting("PSIM_PG_PORT", "5432"),
                                .database = setting("PSIM_PG_DATABASE", "psim"),
                                .user = setting("PSIM_PG_USER", "psim"),
                                .password = setting("PSIM_PG_PASSWORD"),
                                .properties = {{"sslmode", setting("PSIM_PG_SSLMODE", "verify-full")},
                                               {"sslrootcert", setting("PSIM_PG_SSLROOTCERT")},
                                               {"sslcert", setting("PSIM_PG_SSLCERT")},
                                               {"sslkey", setting("PSIM_PG_SSLKEY")}}};
  pg::Migrator migrator(manifest.at("schema").get<std::string>());
  const auto phase =
      std::string_view(arguments[1]) == "expand" ? pg::MigrationPhase::kExpand : pg::MigrationPhase::kContract;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): named closure lives until io.run finishes
  auto apply = [&]() -> asio::awaitable<psim::platform::Result<void>> {
    auto connection = co_await pg::Connection::connect(std::move(options));
    if (!connection) {
      co_return std::unexpected(connection.error());
    }
    co_return co_await migrator.apply(connection->get(), std::move(migrations), phase);
  };
  asio::io_context io;
  auto operation = asio::co_spawn(io, apply(), asio::use_future);
  io.run();
  const auto result = operation.get();
  if (!result) {
    std::println(stderr, "psim-pg-migrate: {}", result.error().message());
    return 1;
  }
  std::println("PostgreSQL migrations applied");
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception&) {
    // Parser diagnostics can contain SQL values. Do not print the exception text.
    (void)std::fputs("psim-pg-migrate: invalid migration manifest or execution failure\n", stderr);
    return 1;
  }
}
