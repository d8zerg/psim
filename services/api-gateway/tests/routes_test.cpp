#include "routes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <string>

#include "proxy.hpp"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::api_gateway {
TEST(GatewayRoutes, EveryContractOperationHasOneRouteAndKnownPolicy) {
  std::set<std::string> operations;
  const platform::security::Principal principal{
      .subject = "user",
      .tenant = "tenant",
      .roles = {"operator", "supervisor", "admin", "integrator", "auditor", "manager"},
      .scopes = {"*"},
      .expires_at = std::chrono::system_clock::time_point::max()};
  ASSERT_GE(routes().size(), 100U);
  for (const auto& item : routes()) {
    EXPECT_TRUE(operations.insert(item.operation).second) << item.operation;
    if (!item.action.empty()) {
      EXPECT_TRUE(platform::authz::decide_role(principal, item.action, std::chrono::system_clock::now()))
          << item.action;
    }
    auto path = item.path;
    for (auto start = path.find('{'); start != std::string::npos; start = path.find('{')) {
      path.replace(start, path.find('}', start) - start + 1, "11111111-1111-4111-8111-111111111111");
    }
    auto found = route(item.method, path + "?pageSize=5");
    ASSERT_TRUE(found) << item.operation;
    EXPECT_EQ((*found)->operation, item.operation);
  }
}

TEST(GatewayRoutes, RejectsAmbiguousPathsAndDoesNotRouteUnknownMethods) {
  EXPECT_FALSE(route(http::verb::get, "/v1/commands/../settings"));
  EXPECT_FALSE(route(http::verb::get, "/v1/commands/%2fsettings"));
  EXPECT_FALSE(route(http::verb::get, "/v1/commands//"));
  EXPECT_FALSE(route(http::verb::get, "/v1/commands/one/extra"));
  EXPECT_FALSE(route(http::verb::delete_, "/v1/commands"));
  auto found = route(http::verb::get, "/v1/events/rule-statistics");
  ASSERT_TRUE(found);
  EXPECT_EQ((*found)->action, "rule.statistics.read");
  found = route(http::verb::get, "/v1/incidents/one/events");
  ASSERT_TRUE(found);
  EXPECT_EQ((*found)->service, "history");
}

TEST(GatewayFilters, UsesRequestedScopeAndRejectsUnknownOrMalformedFilters) {
  auto filter = feed_filter(
      "/v1/feed/incidents?siteIds=11111111-1111-4111-8111-111111111111&zoneIds=22222222-2222-4222-8222-222222222222");
  ASSERT_TRUE(filter);
  EXPECT_EQ(filter->sites.size(), 1U);
  EXPECT_EQ(filter->zones.size(), 1U);
  EXPECT_FALSE(feed_filter("/v1/feed/incidents?siteIds=invalid"));
  EXPECT_FALSE(feed_filter("/v1/feed/incidents?unknown=11111111-1111-4111-8111-111111111111"));
  EXPECT_FALSE(feed_filter("/v1/feed/operators?zoneIds=22222222-2222-4222-8222-222222222222", false));
}

TEST(GatewayHeaders, PreconditionsAndIdempotencyAreCheckedBeforeForwarding) {
  auto selected = route(http::verb::post, "/v1/commands/one:approve");
  ASSERT_TRUE(selected);
  http::fields fields;
  EXPECT_FALSE(request_headers(**selected, fields));
  fields.set("Idempotency-Key", "11111111-1111-4111-8111-111111111111");
  auto result = request_headers(**selected, fields);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code(), platform::ErrorCode::kCommonPreconditionRequired);
  fields.set(http::field::if_match, "\"1\"");
  EXPECT_TRUE(request_headers(**selected, fields));
  fields.set(http::field::if_match, "*");
  EXPECT_FALSE(request_headers(**selected, fields));
}

TEST(GatewayHeaders, StripsUntrustedForwardingAndConnectionHeaders) {
  http::fields input;
  http::fields output;
  input.set("Authorization", "Bearer token");
  input.set("Idempotency-Key", "key");
  input.set("If-Match", "\"5\"");
  input.set("X-Forwarded-User", "admin");
  input.set("Connection", "keep-alive");
  input.set("Cookie", "admin=1");
  request_fields(input, output);
  EXPECT_EQ(output[http::field::authorization], "Bearer token");
  EXPECT_EQ(output[http::field::if_match], "\"5\"");
  EXPECT_EQ(output.count("X-Forwarded-User"), 0U);
  EXPECT_EQ(output.count(http::field::connection), 0U);
  EXPECT_EQ(output.count(http::field::cookie), 0U);
  input.set("Set-Cookie", "session=1");
  input.set("ETag", "\"5\"");
  response_fields(input, output);
  EXPECT_EQ(output[http::field::etag], "\"5\"");
  EXPECT_EQ(output.count(http::field::set_cookie), 0U);
  security_headers(output, "https://console.example");
  EXPECT_EQ(output[http::field::x_content_type_options], "nosniff");
  EXPECT_EQ(output[http::field::access_control_allow_origin], "https://console.example");
  EXPECT_FALSE(allowed_origin("https://evil.example", {"https://console.example"}));
}

TEST(GatewayRateLimit, IdentityIsolationRefillAndBoundedCapacity) {
  RateLimiter limit(2, 2, 2);
  auto now = std::chrono::steady_clock::now();
  EXPECT_TRUE(limit.allow("one", now));
  EXPECT_TRUE(limit.allow("one", now));
  EXPECT_FALSE(limit.allow("one", now));
  EXPECT_TRUE(limit.allow("two", now));
  EXPECT_FALSE(limit.allow("three", now));
  EXPECT_TRUE(limit.allow("one", now + std::chrono::milliseconds(500)));
  EXPECT_TRUE(limit.allow("three", now + std::chrono::minutes(6)));
}
}  // namespace psim::api_gateway
