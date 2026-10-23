#include "realtime.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "psim/platform/kafka/client.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/realtime/v1/realtime.pb.h"

namespace psim::api_gateway {
TEST(RealtimeResume, BothTopicVectorsRoundTripWithoutProcessIdentity) {
  const platform::kafka::Offsets offsets{{{kIncidentTopic, 0}, 17}, {{kResponseTopic, 1}, 22}};
  auto decoded = resume_positions(resume_token(offsets, "tenant"), "tenant");
  ASSERT_TRUE(decoded);
  EXPECT_EQ(*decoded, offsets);
  EXPECT_FALSE(resume_positions(resume_token(offsets, "tenant"), "other"));
}

TEST(RealtimeResume, RejectsMalformedFutureAndExpiredPositions) {
  const auto original = nlohmann::json::parse(resume_token({{{kIncidentTopic, 0}, 0}}, "tenant"));
  for (const auto& invalid :
       {nlohmann::json{{"offsets", {{"unknown-partition", 0}}}}, nlohmann::json{{"offsets", {{"0", -1}}}},
        nlohmann::json{{"offsets", {{"01", 0}}}}, nlohmann::json{{"version", 1}},
        nlohmann::json{{"issuedAtMs", -9223372036854775807LL - 1}}, nlohmann::json{{"issuedAtMs", 1e40}},
        nlohmann::json{{"issuedAtMs", 0}}, nlohmann::json{{"issuedAtMs", 9223372036854775807LL}}}) {
    auto token = original;
    token.update(invalid);
    EXPECT_FALSE(resume_positions(token.dump(), "tenant")) << token;
  }
  EXPECT_FALSE(resume_positions("broken", "tenant"));
}

TEST(RealtimeAuthorization, TenantScopeAndFiltersIntersect) {
  platform::security::Principal principal{.subject = "operator",
                                          .tenant = "tenant",
                                          .roles = {"operator"},
                                          .scopes = {"site:site"},
                                          .expires_at = std::chrono::system_clock::now() + std::chrono::minutes(1)};
  realtime::v1::Subscribe subscription;
  subscription.set_stream(realtime::v1::STREAM_INCIDENTS);
  RealtimeEvent event;
  event.tenant = "tenant";
  event.site = "site";
  event.zone = "zone";
  event.incident = "incident";
  EXPECT_TRUE(realtime_filter(principal, subscription, event));
  subscription.mutable_filter()->add_zone_ids("other");
  EXPECT_FALSE(realtime_filter(principal, subscription, event));
  subscription.mutable_filter()->clear_zone_ids();
  subscription.set_stream(realtime::v1::STREAM_INCIDENT);
  subscription.mutable_filter()->set_incident_id("incident");
  EXPECT_TRUE(realtime_filter(principal, subscription, event));
  event.incident = "other";
  EXPECT_FALSE(realtime_filter(principal, subscription, event));
  event.incident = "incident";
  event.tenant = "other";
  EXPECT_FALSE(realtime_filter(principal, subscription, event));
  principal.scopes = {"zone:zone"};
  event.tenant = "tenant";
  EXPECT_TRUE(realtime_filter(principal, subscription, event));
}

TEST(RealtimeAuthorization, SharedLocationViewsRequireIdenticalRights) {
  platform::security::Principal first;
  first.tenant = "tenant";
  first.roles = {"operator"};
  first.scopes = {"site:one"};
  auto other = first;
  other.subject = "another-user";
  EXPECT_EQ(scope_key(first), scope_key(other));
  other.scopes = {"site:two"};
  EXPECT_NE(scope_key(first), scope_key(other));
  other = first;
  other.tenant = "other";
  EXPECT_NE(scope_key(first), scope_key(other));
}
}  // namespace psim::api_gateway
