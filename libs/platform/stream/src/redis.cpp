#include "psim/platform/stream/redis.hpp"

#include <string>
#include <vector>

#include "psim/platform/config.hpp"
#include "psim/platform/redis/pool.hpp"

namespace psim::platform::stream {
redis::PoolOptions redis_options(const config::Config& config) {
  redis::PoolOptions options;
  auto& connection = options.connection;
  connection.endpoint.host = config.get<std::string>("settings.redis_host");
  connection.endpoint.port = config.get<std::string>("settings.redis_port");
  connection.credentials.username = config.get<std::string>("settings.redis_user");
  if (const auto secret = config.secret("settings.redis_password_file")) {
    connection.credentials.password = std::string(*secret);
  }
  connection.tls.enabled = !config.get<bool>("settings.allow_plaintext");
  connection.tls.ca_file = config.get<std::string>("settings.redis_ca_file");
  connection.tls.certificate_file = config.get<std::string>("settings.redis_certificate_file");
  connection.tls.private_key_file = config.get<std::string>("settings.redis_key_file");
  auto& sentinel = connection.sentinel;
  sentinel.tls = connection.tls;
  sentinel.primary_name = config.get<std::string>("settings.redis_primary_name");
  sentinel.credentials.username = config.get<std::string>("settings.redis_sentinel_user");
  if (const auto secret = config.secret("settings.redis_sentinel_password_file")) {
    sentinel.credentials.password = std::string(*secret);
  }
  for (const auto& endpoint : config.get<std::vector<std::string>>("settings.redis_sentinels")) {
    const auto separator = endpoint.rfind(':');
    sentinel.endpoints.push_back({endpoint.substr(0, separator), endpoint.substr(separator + 1)});
  }
  return options;
}
}  // namespace psim::platform::stream
