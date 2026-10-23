#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "console_replay.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/security/secret.hpp"

namespace {
std::string env(const char* name, std::string fallback = "") {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): read before any worker thread starts.
  return value ? std::string(value) : std::move(fallback);
}
}  // namespace

int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  if (argc != 2 || std::string_view(args[1]) != "rebuild") {
    std::cerr << "usage: psim-console-tool rebuild\n";
    return 2;
  }
  try {
    namespace p = psim::platform;
    p::pg::PoolOptions database;
    database.connection.host = env("PSIM_PG_HOST", "postgres");
    database.connection.port = env("PSIM_PG_PORT", "5432");
    database.connection.database = env("PSIM_PG_DATABASE", "psim");
    database.connection.user = env("PSIM_PG_USER", "psim");
    auto password = p::security::read_secret(env("PSIM_PG_PASSWORD_FILE"));
    if (!password) {
      throw std::runtime_error(password.error().message());
    }
    database.connection.password = std::string(password->view());
    database.connection.properties = {{"sslmode", env("PSIM_PG_SSLMODE", "verify-full")},
                                      {"sslrootcert", env("PSIM_PG_SSLROOTCERT")},
                                      {"sslcert", env("PSIM_PG_SSLCERT")},
                                      {"sslkey", env("PSIM_PG_SSLKEY")}};
    p::kafka::ClientOptions kafka;
    kafka.bootstrap = env("PSIM_KAFKA_BROKERS", "kafka-1:19092");
    kafka.client_id = "console-rebuild";
    kafka.properties = {{"fetch.wait.max.ms", "1"}};
    kafka.allow_plaintext = env("PSIM_KAFKA_ALLOW_PLAINTEXT") == "1";
    if (!kafka.allow_plaintext) {
      kafka.security = p::kafka::TransportSecurity{.tls = {.ca_file = env("PSIM_KAFKA_CA_FILE"),
                                                           .certificate_file = env("PSIM_KAFKA_CERT_FILE"),
                                                           .private_key_file = env("PSIM_KAFKA_KEY_FILE")}};
    }
    boost::asio::io_context io;
    p::async::BlockingPool blocking(4, 32);
    p::pg::Pool pool(std::move(database));
    auto result = boost::asio::co_spawn(
        io,
        psim::projection::console_rebuild(&pool, std::move(kafka), {.blocking = &blocking}, std::chrono::minutes(10)),
        boost::asio::use_future);
    io.run();
    auto rebuilt = result.get();
    pool.close();
    if (!rebuilt) {
      std::cerr << rebuilt.error().message() << '\n';
      return 1;
    }
    std::cout << "console generation " << *rebuilt << " activated\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
