#include <nlohmann/json.hpp>

#include <cstddef>
#include <map>
#include <string>

#include "psim/platform/error.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "workers.hpp"

namespace psim::api_gateway {
namespace {
using Json = nlohmann::json;
namespace p = platform;

std::string settings() {
  auto schema = Json::parse(p::stream::kSettings);
  auto& fields = schema["properties"];
  fields["listen_plaintext"] = {{"type", "boolean"}, {"default", false}};
  fields["allowed_origins"] = {{"type", "array"}, {"items", {{"type", "string"}}}, {"default", Json::array()}};
  for (const auto* name : {"tls_ca_file", "tls_certificate_file", "tls_private_key_file", "upstream_ca_file",
                           "upstream_certificate_file", "upstream_key_file", "jwks_ca_file", "redis_host",
                           "redis_ca_file", "redis_certificate_file", "redis_key_file"}) {
    fields[name] = {{"type", "string"}, {"default", ""}};
  }
  fields["redis_password_file"] = {{"type", "string"}, {"x-psim-secret", true}};
  fields["redis_host"] = {{"type", "string"}, {"minLength", 1}, {"default", "valkey"}};
  for (const auto* name : {"rate_per_second", "rate_burst", "ip_rate_per_second", "ip_rate_burst"}) {
    fields[name] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 100000}, {"default", 1000}};
  }
  for (const auto& [name, value] : std::map<std::string, std::size_t>{{"realtime_max_connections", 1024},
                                                                      {"realtime_max_subscriptions", 16},
                                                                      {"realtime_queue_messages", 1000},
                                                                      {"realtime_queue_bytes", 4194304},
                                                                      {"realtime_total_queue_bytes", 67108864}}) {
    fields[name] = {{"type", "integer"},
                    {"minimum", 1},
                    {"maximum", name.ends_with("bytes") ? 268435456 : 10000},
                    {"default", value}};
  }
  for (const auto& [name, host] : std::map<std::string, std::string>{{"catalog", "resource-catalog"},
                                                                     {"rules", "rule-management"},
                                                                     {"incidents", "incident-service"},
                                                                     {"commands", "command-service"},
                                                                     {"response", "response-engine"},
                                                                     {"history", "event-history"},
                                                                     {"reports", "projection-service"},
                                                                     {"projection", "projection-service"},
                                                                     {"audit", "audit-service"},
                                                                     {"notifications", "notification-service"}}) {
    fields[name + "_host"] = {{"type", "string"}, {"minLength", 1}, {"default", host}};
    fields[name + "_port"] = {{"type", "string"}, {"pattern", "^[0-9]{1,5}$"}, {"default", "8443"}};
  }
  return schema.dump();
}
}  // namespace
}  // namespace psim::api_gateway

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "api-gateway",
                                       .settings_schema = psim::api_gateway::settings(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(psim::api_gateway::gateway(context));
                                         return result;
                                       }});
}
