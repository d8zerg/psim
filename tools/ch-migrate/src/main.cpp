#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "psim/platform/pg/pool.hpp"

namespace {
namespace p = psim::platform;

std::string setting(const char* name, std::string fallback = {}) {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): startup before threads
  return value ? std::string(value) : std::move(fallback);
}

int run(std::span<char*> arguments) {
  if (arguments.size() != 3
      || (std::string_view(arguments[1]) != "expand" && std::string_view(arguments[1]) != "contract")) {
    std::println(
        stderr,
        "usage: psim-ch-migrate expand|contract manifest.json (connection: PSIM_PG_* and PSIM_CH_* environment)");
    return 2;
  }
  std::ifstream input(arguments[2]);
  const auto document = nlohmann::json::parse(input);
  std::vector<p::clickhouse::Migration> migrations;
  for (const auto& item : document.at("migrations")) {
    migrations.push_back({.version = item.at("version").get<std::int64_t>(),
                          .name = item.at("name").get<std::string>(),
                          .expand = item.at("expand").get<std::vector<std::string>>(),
                          .contract = item.value("contract", std::vector<std::string>{}),
                          .minimum_reader = item.value("minimum_reader", std::int64_t{0})});
  }
  std::ifstream secret(setting("PSIM_PG_PASSWORD_FILE"));
  std::string password;
  std::getline(secret, password);
  if (!secret || password.empty()) {
    throw std::runtime_error("PostgreSQL password file cannot be read");
  }
  p::pg::PoolOptions pg{.connection = {.host = setting("PSIM_PG_HOST", "postgres"),
                                       .port = setting("PSIM_PG_PORT", "15432"),
                                       .database = setting("PSIM_PG_DATABASE", "psim"),
                                       .user = setting("PSIM_PG_USER", "postgres"),
                                       .password = std::move(password),
                                       .properties = {{"sslmode", setting("PSIM_PG_SSLMODE", "verify-full")},
                                                      {"sslrootcert", setting("PSIM_PG_SSLROOTCERT")},
                                                      {"sslcert", setting("PSIM_PG_SSLCERT")},
                                                      {"sslkey", setting("PSIM_PG_SSLKEY")}}}};
  p::clickhouse::PoolOptions ch{.endpoints = {{setting("PSIM_CH_HOST", "clickhouse"), 9440}},
                                .user = setting("PSIM_CH_USER", "deployment-admin"),
                                .password_file = setting("PSIM_CH_PASSWORD_FILE"),
                                .tls = {.ca_file = setting("PSIM_CH_CA"),
                                        .certificate_file = setting("PSIM_CH_CERT"),
                                        .private_key_file = setting("PSIM_CH_KEY")}};
  boost::asio::io_context context;
  p::async::BlockingPool blocking(2, 8);
  p::pg::Pool journal(std::move(pg));
  p::clickhouse::Pool destination(&blocking, std::move(ch));
  p::clickhouse::Migrator migrator(&destination, &journal, document.at("target").get<std::string>());
  auto result =
      boost::asio::co_spawn(context,
                            migrator.apply(std::move(migrations), std::string_view(arguments[1]) == "expand"
                                                                      ? p::clickhouse::MigrationPhase::kExpand
                                                                      : p::clickhouse::MigrationPhase::kContract),
                            boost::asio::use_future);
  context.run();
  auto completed = result.get();
  if (!completed) {
    std::println(stderr, "psim-ch-migrate: {}", completed.error().message());
    return 1;
  }
  std::println("ClickHouse migration phase completed");
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception&) {
    (void)std::fputs("psim-ch-migrate: invalid manifest or migration failure\n", stderr);
    return 1;
  }
}
