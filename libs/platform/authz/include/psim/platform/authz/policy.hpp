#pragma once
#include <chrono>
#include <string_view>

#include "psim/platform/security/jwt.hpp"

namespace psim::platform::authz {
struct Resource {
  std::string_view tenant;
  std::string_view site{};
  std::string_view zone{};
};

/// Evaluate verified identity and resource scope. Persistence of denials belongs to the caller.
[[nodiscard]] Result<void> decide(const security::Principal& principal, std::string_view action, Resource resource,
                                  std::chrono::system_clock::time_point now);
[[nodiscard]] bool scoped(const security::Principal& principal, Resource resource);
}  // namespace psim::platform::authz
