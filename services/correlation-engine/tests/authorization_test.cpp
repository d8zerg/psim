#include <gtest/gtest.h>

#include <chrono>

#include "psim/platform/security/jwt.hpp"
#include "repository.hpp"

namespace psim::correlation::management {
namespace {
TEST(RuleAuthorization, WritesRequireAdminAndGlobalScope) {
  p::security::Principal principal{.subject = "11111111-1111-4111-8111-111111111111",
                                   .tenant = "22222222-2222-4222-8222-222222222222",
                                   .roles = {"admin"},
                                   .scopes = {"*"},
                                   .expires_at = std::chrono::system_clock::now() + std::chrono::hours(1)};
  EXPECT_TRUE(can_write(principal));
  principal.scopes = {principal.tenant};
  EXPECT_TRUE(can_read(principal));
  EXPECT_TRUE(can_read_site(principal, principal.tenant));
  EXPECT_FALSE(can_read_site(principal, principal.subject));
  EXPECT_FALSE(can_write(principal));
  principal.scopes = {"*"};
  for (const auto* role : {"integrator", "supervisor", "auditor"}) {
    principal.roles = {role};
    EXPECT_TRUE(can_read(principal));
    EXPECT_FALSE(can_write(principal));
  }
  principal.roles = {"operator"};
  EXPECT_FALSE(can_read(principal));
  principal.roles = {"admin"};
  principal.expires_at = {};
  EXPECT_FALSE(can_write(principal));
  principal.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);
  principal.tenant = "invalid";
  EXPECT_FALSE(can_write(principal));
}
}  // namespace
}  // namespace psim::correlation::management
