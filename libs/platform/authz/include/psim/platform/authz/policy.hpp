#pragma once
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/security/jwt.hpp"

namespace psim::platform::authz {
struct ScopeFilter {
  bool all = false;
  std::vector<std::string> sites;
  std::vector<std::string> zones;
};

[[nodiscard]] bool all_scopes(const security::Principal& principal);
[[nodiscard]] bool any_scope(const security::Principal& principal);
[[nodiscard]] bool covers_scopes(const security::Principal& principal, const security::Principal& required);
[[nodiscard]] ScopeFilter scope_filter(const security::Principal& principal);

struct Resource {
  std::string_view tenant;
  std::string_view site{};
  std::string_view zone{};
};

/// Check identity and role before the resource-owning service resolves its scope.
[[nodiscard]] Result<void> decide_role(const security::Principal& principal, std::string_view action,
                                       std::chrono::system_clock::time_point now);
[[nodiscard]] Result<void> decide_somewhere(const security::Principal& principal, std::string_view action,
                                            std::chrono::system_clock::time_point now);
[[nodiscard]] std::vector<std::string> actions(const security::Principal& principal,
                                               std::chrono::system_clock::time_point now);

/// Evaluate verified identity and resource scope. Persistence of denials belongs to the caller.
[[nodiscard]] Result<void> decide(const security::Principal& principal, std::string_view action, Resource resource,
                                  std::chrono::system_clock::time_point now);
[[nodiscard]] bool scoped(const security::Principal& principal, Resource resource);
}  // namespace psim::platform::authz
