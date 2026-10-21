#include "psim/platform/authz/policy.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::platform::authz {
TEST(Authorization, RoleAndScopeMatrixHasNoAdministratorBypass) {
  const auto now = std::chrono::system_clock::time_point(std::chrono::seconds(100));
  for (const auto* role : {"operator", "supervisor", "admin", "integrator", "auditor", "manager", "unknown"}) {
    const security::Principal principal{.subject = "user",
                                        .tenant = "tenant",
                                        .roles = {role},
                                        .scopes = {"site:a"},
                                        .expires_at = now + std::chrono::seconds(5)};
    EXPECT_EQ(decide(principal, "incident.acknowledge", {"tenant", "a", "zone"}, now).has_value(),
              std::string(role) == "operator" || std::string(role) == "supervisor");
    EXPECT_EQ(decide(principal, "incident.read", {"tenant", "a", "zone"}, now).has_value(),
              std::string(role) == "operator" || std::string(role) == "supervisor" || std::string(role) == "auditor");
    EXPECT_FALSE(decide(principal, "incident.read", {"other", "a", "zone"}, now));
    EXPECT_FALSE(decide(principal, "incident.read", {"tenant", "other", "other"}, now));
    EXPECT_FALSE(decide(principal, "unregistered.action", {"tenant", "a"}, now));
  }
}

TEST(Authorization, ExpiredEmptyAndCrossTenantPrincipalsAreRejected) {
  const auto now = std::chrono::system_clock::time_point{};
  security::Principal principal{.subject = "user",
                                .tenant = "tenant",
                                .roles = {"supervisor"},
                                .scopes = {"*"},
                                .expires_at = now + std::chrono::seconds(5)};
  EXPECT_TRUE(decide(principal, "incident.close", {"tenant", "a", "b"}, now));
  EXPECT_FALSE(decide(principal, "incident.close", {"other", "a", "b"}, now));
  principal.scopes = {"zone:b"};
  EXPECT_TRUE(decide(principal, "incident.close", {"tenant", "a", "b"}, now));
  principal.scopes = {"b"};
  EXPECT_TRUE(decide(principal, "incident.close", {"tenant", "a", "b"}, now));
  principal.scopes = {"a"};
  EXPECT_TRUE(decide(principal, "incident.close", {"tenant", "a", "b"}, now));
  principal.scopes = {};
  EXPECT_EQ(decide(principal, "incident.close", {"tenant", "a", "b"}, now).error().code(), ErrorCode::kAuthScopeDenied);
  principal.expires_at = now;
  EXPECT_EQ(decide(principal, "incident.close", {"tenant", "a", "b"}, now).error().code(),
            ErrorCode::kAuthUnauthenticated);
  principal.expires_at = now + std::chrono::seconds(1);
  principal.scopes = {"*"};
  principal.tenant.clear();
  EXPECT_FALSE(decide(principal, "incident.types.read", {""}, now));
  principal.subject.clear();
  EXPECT_FALSE(decide(principal, "incident.close", {"tenant"}, now));
}
}  // namespace psim::platform::authz
