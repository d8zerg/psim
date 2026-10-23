#include "psim/platform/authz/policy.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
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
    EXPECT_EQ(decide_role(principal, "rule.write", now).has_value(), std::string(role) == "admin");
  }
}

TEST(Authorization, EveryActionChecksRoleTenantScopeAndExpiry) {
  std::ifstream file(PSIM_AUTHZ_ACTIONS);
  const auto actions = nlohmann::json::parse(file);
  const auto now = std::chrono::system_clock::now();
  for (const auto& [action, roles] : actions.items()) {
    for (const auto* role : {"operator", "supervisor", "admin", "integrator", "auditor", "manager", "unknown"}) {
      bool expected = false;
      for (const auto& allowed : roles) {
        expected = expected || allowed == role;
      }
      security::Principal principal{.subject = "user",
                                    .tenant = "tenant",
                                    .roles = {role},
                                    .scopes = {"site:one"},
                                    .expires_at = now + std::chrono::minutes(1)};
      SCOPED_TRACE(action + ":" + role);
      EXPECT_EQ(decide_role(principal, action, now).has_value(), expected);
      for (const auto* scope : {"*", "one", "site:one", "zone:zone"}) {
        principal.scopes = {scope};
        EXPECT_EQ(decide(principal, action, {"tenant", "one", "zone"}, now).has_value(), expected);
        EXPECT_FALSE(decide(principal, action, {"foreign", "one", "zone"}, now));
      }
      principal.scopes = {"site:unrelated", "zone:unrelated"};
      EXPECT_EQ(decide(principal, action, {"tenant", "one", "zone"}, now).has_value(),
                expected && action == "incident.types.read");
      EXPECT_EQ(decide_somewhere(principal, action, now).has_value(), expected);
      principal.scopes.clear();
      EXPECT_FALSE(decide_somewhere(principal, action, now));
      EXPECT_FALSE(decide(principal, action, {"tenant", "one", "zone"}, now));
      principal.scopes = {"*"};
      principal.expires_at = now;
      EXPECT_FALSE(decide(principal, action, {"tenant", "one", "zone"}, now));
    }
  }
}

TEST(Authorization, NormalizedScopesCoverEquivalentSavedGrantsWithoutCrossTenantExpansion) {
  security::Principal current{.tenant = "tenant", .scopes = {"site:one", "zone:two"}};
  security::Principal required{.tenant = "tenant", .scopes = {"one", "zone:two"}};
  EXPECT_TRUE(covers_scopes(current, required));
  required.scopes = {"zone:one"};
  EXPECT_FALSE(covers_scopes(current, required));
  required.scopes = {"*"};
  EXPECT_FALSE(covers_scopes(current, required));
  current.scopes = {"*"};
  EXPECT_TRUE(covers_scopes(current, required));
  required.tenant = "foreign";
  EXPECT_FALSE(covers_scopes(current, required));
  current.scopes = {"site:", "zone:", ""};
  EXPECT_FALSE(any_scope(current));
  const auto filter = scope_filter(current);
  EXPECT_TRUE(filter.sites.empty());
  EXPECT_TRUE(filter.zones.empty());
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
