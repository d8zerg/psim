#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "chain.hpp"
#include "psim/platform/build_info.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/pg/connection.hpp"
#include "repository.hpp"

namespace {
namespace a = psim::audit_service;
namespace asio = boost::asio;

std::string setting(const char* name, std::string fallback = {}) {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): single-threaded CLI startup.
  return value ? std::string(value) : std::move(fallback);
}

int run(std::span<char*> args) {
  if (args.size() == 2 && std::string_view(args[1]) == "--version") {
    std::println("psim-audit-tool {}", psim::platform::build_info().version);
    return 0;
  }
  if (args.size() == 3 && std::string_view(args[1]) == "migrate") {
    psim::platform::pg::ConnectionOptions options{.host = setting("PSIM_PG_HOST", "127.0.0.1"),
                                                  .port = setting("PSIM_PG_PORT", "5432"),
                                                  .database = setting("PSIM_PG_DATABASE", "psim"),
                                                  .user = setting("PSIM_PG_USER", "psim"),
                                                  .password = setting("PSIM_PG_PASSWORD"),
                                                  .properties = {{"sslmode", setting("PSIM_PG_SSLMODE", "verify-full")},
                                                                 {"sslrootcert", setting("PSIM_PG_SSLROOTCERT")},
                                                                 {"sslcert", setting("PSIM_PG_SSLCERT")},
                                                                 {"sslkey", setting("PSIM_PG_SSLKEY")}}};
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure lives until io.run completes.
    auto migrate = [&]() -> asio::awaitable<psim::platform::Result<void>> {
      auto connection = co_await psim::platform::pg::Connection::connect(std::move(options));
      if (!connection) {
        co_return std::unexpected(connection.error());
      }
      co_return co_await a::migrate(connection->get(), args[2]);
    };
    asio::io_context io;
    auto result = asio::co_spawn(io, migrate(), asio::use_future);
    io.run();
    if (!result.get()) {
      std::println(stderr, "audit migration failed");
      return 1;
    }
    std::println("audit migration applied");
    return 0;
  }
  if (args.size() != 5 || std::string_view(args[1]) != "verify") {
    std::println(
        stderr,
        "usage: psim-audit-tool migrate append_role | verify export.jsonl trusted-keys.json checkpoint-directory");
    return 2;
  }
  if (std::filesystem::file_size(args[2]) > std::size_t{64} * 1024 * 1024) {
    throw std::invalid_argument("audit export exceeds limit");
  }
  std::ifstream input(args[2]);
  std::ifstream keys(args[3]);
  const a::Signer signer({}, a::Json::parse(keys));
  std::vector<a::Entry> entries;
  std::vector<a::Checkpoint> bundled;
  a::Json header;
  a::Json footer;
  std::string line;
  std::size_t size = 0;
  while (std::getline(input, line)) {
    size += line.size() + 1;
    if (size > static_cast<std::size_t>(64 * 1024 * 1024) || !footer.is_null()) {
      throw std::invalid_argument("invalid export framing");
    }
    const auto value = a::Json::parse(line);
    if (header.is_null()) {
      if (value.at("format") != "psim.audit.export.v1") {
        throw std::invalid_argument("unsupported export format");
      }
      header = value;
    } else if (value.contains("record")) {
      if (!bundled.empty()) {
        throw std::invalid_argument("invalid record order");
      }
      auto record = a::parse<a::Entry>(value.at("record"));
      if (value.at("selected") != a::matches(record, header.at("query"))) {
        throw std::invalid_argument("export selection mismatch");
      }
      entries.push_back(std::move(record));
    } else if (value.contains("checkpoint")) {
      bundled.push_back(a::parse<a::Checkpoint>(value.at("checkpoint")));
    } else {
      footer = value;
    }
  }
  const auto tenant = header.at("tenantId").get<std::string>();
  const auto external = a::Archive(args[4]).read(tenant);
  bundled.insert(bundled.end(), external.begin(), external.end());
  const auto result = a::verify(entries, bundled, signer, tenant);
  const auto head = entries.empty() ? std::string(32, '\0') : entries.back().hash();
  const bool framed = footer.at("headSeq") == std::to_string(entries.size()) && footer.at("headHash") == a::hex(head);
  const bool anchored =
      entries.empty() ? external.empty() : !external.empty() && external.back().audit_seq() == entries.size();
  std::println(
      "{}", a::Json{{"intact", result.intact && framed && anchored},
                    {"recordsChecked", result.checked},
                    {"firstBrokenSeq", result.broken},
                    {"detail", !anchored ? "export head requires an independent external checkpoint" : result.detail}}
                .dump());
  return result.intact && framed && anchored ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception&) {
    (void)std::fputs("audit operation failed: invalid input, storage or key configuration\n", stderr);
    return 1;
  }
}
