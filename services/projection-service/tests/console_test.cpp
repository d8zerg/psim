#include <gtest/gtest.h>

#include "console_api.hpp"

namespace psim::projection {
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

}  // namespace psim::projection
