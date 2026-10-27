#include "release.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/pg/connection.hpp"

namespace {
namespace pg = psim::platform::pg;
namespace asio = boost::asio;
using Json = nlohmann::json;

void require(bool value, std::string_view message) {
  if (!value) {
    throw std::runtime_error(std::string(message));
  }
}

template <class T>
T take(psim::platform::Result<T> result) {
  require(result.has_value(), "release migration database operation failed");
  return std::move(*result);
}

Json document(const std::filesystem::path& path) {
  require(std::filesystem::is_regular_file(path) && !std::filesystem::is_symlink(path)
              && std::filesystem::file_size(path) <= std::size_t{4} * 1024 * 1024,
          "invalid release migration document");
  std::ifstream input(path);
  return Json::parse(input);
}

std::string checksum(const Json& statements) {
  const auto bytes = statements.dump(-1, ' ', true);
  std::array<unsigned char, EVP_MAX_MD_SIZE> output{};
  unsigned int size = 0;
  require(EVP_Digest(bytes.data(), bytes.size(), output.data(), &size, EVP_sha256(), nullptr) == 1,
          "migration checksum failed");
  std::ostringstream value;
  for (unsigned int i = 0; i < size; ++i) {
    value << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(output.at(i));
  }
  return value.str();
}

asio::awaitable<void> apply(pg::ConnectionOptions options, std::filesystem::path directory, std::string phase,
                            std::int64_t minimum_epoch) {
  const auto manifest = document(directory / "release.json");
  require(phase == "expand" || phase == "contract", "invalid release migration phase");
  const auto epoch = manifest.value("schema_epoch", std::int64_t{2});
  require(epoch > 0 && epoch < 1000000 && (phase != "contract" || minimum_epoch >= epoch + 1),
          "contract requires a later epoch and drained old readers");
  auto connection = take(co_await pg::Connection::connect(std::move(options)));
  auto locked = take(co_await connection->query(
      {.sql = "SELECT pg_try_advisory_lock(hashtextextended('psim-release-migrations',0))"}));
  require(locked.value(0, 0) == "t", "another release migration is running");
  (void)take(co_await connection->query({.sql = "SET lock_timeout='2s'"}));
  (void)take(co_await connection->query({.sql = "SET statement_timeout='30s'"}));
  (void)take(co_await connection->query({.sql = "CREATE SCHEMA IF NOT EXISTS psim_release"}));
  (void)take(co_await connection->query(
      {.sql =
           "CREATE TABLE IF NOT EXISTS psim_release.migrations (name text NOT NULL, version bigint NOT NULL, phase "
           "text NOT NULL, checksum text NOT NULL, step bigint NOT NULL DEFAULT 0, PRIMARY KEY(name,version,phase))"}));
  for (const auto& entry : manifest.value("migrations", Json::array())) {
    if (entry.at("engine") != "postgres") {
      continue;
    }
    const auto filename = entry.at("file").get<std::string>();
    const std::filesystem::path relative(filename);
    require(!relative.is_absolute() && relative.parent_path().empty(),
            "release migration file must be a direct bundle member");
    const auto plan = document(directory / relative);
    for (const auto& migration : plan.at("migrations")) {
      const auto statements = migration.value(phase, Json::array());
      require(statements.is_array(), "migration statements must be an array");
      if (statements.empty()) {
        continue;
      }
      const auto version = migration.at("version").get<std::int64_t>();
      require(version > 0 && statements.size() <= 10000, "invalid migration bounds");
      const auto digest = checksum(statements);
      const std::vector<pg::Parameter> key{pg::Parameter::text(filename), pg::Parameter::integer(version),
                                           pg::Parameter::text(phase)};
      auto values = key;
      values.push_back(pg::Parameter::text(digest));
      (void)take(co_await connection->query({.sql = "INSERT INTO psim_release.migrations(name,version,phase,checksum) "
                                                    "VALUES ($1,$2,$3,$4) ON CONFLICT DO NOTHING",
                                             .parameters = std::move(values)}));
      auto state = take(co_await connection->query(
          {.sql = "SELECT checksum,step FROM psim_release.migrations WHERE name=$1 AND version=$2 AND phase=$3",
           .parameters = key}));
      const auto step = take(state.integer(0, 1));
      require(state.value(0, 0) == digest && step >= 0 && std::cmp_less_equal(step, statements.size()),
              "applied release migration plan was modified");
      for (auto index = static_cast<std::size_t>(step); index < statements.size(); ++index) {
        // Each trusted statement is replayable; autocommit also supports concurrent index creation.
        (void)take(co_await connection->query({.sql = statements.at(index).get<std::string>()}));
        auto progress = key;
        progress.push_back(pg::Parameter::integer(static_cast<std::int64_t>(index + 1)));
        (void)take(co_await connection->query(
            {.sql = "UPDATE psim_release.migrations SET step=$4 WHERE name=$1 AND version=$2 AND phase=$3",
             .parameters = std::move(progress)}));
      }
    }
  }
}
}  // namespace

int migrate_release(pg::ConnectionOptions options, std::filesystem::path directory, std::string phase,
                    std::int64_t minimum_epoch) {
  asio::io_context io;
  auto future = asio::co_spawn(io, apply(std::move(options), std::move(directory), std::move(phase), minimum_epoch),
                               asio::use_future);
  io.run();
  future.get();
  return 0;
}
