#include "psim/platform/stream/redis.hpp"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <utility>

#include "psim/platform/config.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::platform::stream {
namespace {
config::Config defaults() {
  auto schema = config::Schema::parse(runtime::configuration_schema(kSettings));
  return config::load(*schema, {}).value();
}
}  // namespace

TEST(StreamRedis, StandaloneDefaultsKeepExistingAuthenticationAndEnableVerifiedTls) {
  const auto options = redis_options(defaults());
  EXPECT_EQ(options.connection.endpoint.port, "6379");
  EXPECT_EQ(options.connection.credentials.username, "default");
  EXPECT_TRUE(options.connection.tls.enabled);
  EXPECT_TRUE(options.connection.sentinel.endpoints.empty());
}

TEST(StreamRedis, DedicatedAccountsAndSentinelsUseTheSameVerifiedClientIdentity) {
  auto body = defaults().document();
  auto& settings = body["settings"];
  settings["redis_host"] = "valkey-1";
  settings["redis_port"] = "6380";
  settings["redis_user"] = "connector-gateway";
  settings["redis_ca_file"] = "/ca.pem";
  settings["redis_certificate_file"] = "/service.pem";
  settings["redis_key_file"] = "/service.key";
  settings["redis_sentinels"] = {"infra-1:26379", "infra-2:26379", "infra-3:26379"};
  settings["redis_sentinel_user"] = "gateway-discovery";
  settings["redis_primary_name"] = "psim-primary";
  const config::Config config(std::move(body), {{"settings.redis_password_file", "data-password"},
                                                {"settings.redis_sentinel_password_file", "discovery-password"}});
  const auto options = redis_options(config);
  EXPECT_EQ(options.connection.endpoint.port, "6380");
  EXPECT_EQ(options.connection.credentials.username, "connector-gateway");
  EXPECT_EQ(options.connection.credentials.password, "data-password");
  const auto& sentinel = options.connection.sentinel;
  ASSERT_EQ(sentinel.endpoints.size(), 3);
  EXPECT_EQ(sentinel.endpoints.at(2).host, "infra-3");
  EXPECT_EQ(sentinel.endpoints.at(2).port, "26379");
  EXPECT_EQ(sentinel.primary_name, "psim-primary");
  EXPECT_EQ(sentinel.credentials.username, "gateway-discovery");
  EXPECT_EQ(sentinel.credentials.password, "discovery-password");
  EXPECT_TRUE(sentinel.tls.enabled);
  EXPECT_EQ(sentinel.tls.ca_file, "/ca.pem");
  EXPECT_EQ(sentinel.tls.certificate_file, "/service.pem");
  EXPECT_EQ(sentinel.tls.private_key_file, "/service.key");
  EXPECT_FALSE(config.document().dump().contains("data-password"));
}
}  // namespace psim::platform::stream
