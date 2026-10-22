#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/runtime.hpp"
#include "workers.hpp"

namespace psim::projection {
std::unique_ptr<p::clickhouse::Pool> connection(const p::runtime::Context& context, bool reader) {
  const auto& c = context.config();
  const auto read = reader && !c.get<std::string>("settings.reports_read_user").empty();
  return std::make_unique<p::clickhouse::Pool>(
      &context.blocking(),
      p::clickhouse::PoolOptions{
          .endpoints = {{c.get<std::string>("settings.clickhouse_host"),
                         c.get<std::uint16_t>("settings.clickhouse_port")}},
          .user = c.get<std::string>(read ? "settings.reports_read_user" : "settings.clickhouse_user"),
          .password_file =
              c.get<std::string>(read ? "settings.reports_read_password_file" : "settings.clickhouse_password_file"),
          .tls = {.ca_file = c.get<std::string>("settings.clickhouse_ca_file"),
                  .certificate_file = c.get<std::string>("settings.clickhouse_certificate_file"),
                  .private_key_file = c.get<std::string>("settings.clickhouse_private_key_file")},
          .allow_plaintext = c.get<bool>("settings.allow_plaintext"),
          .query_timeout = std::chrono::milliseconds(reader ? 3000 : 30000)});
}
}  // namespace psim::projection
