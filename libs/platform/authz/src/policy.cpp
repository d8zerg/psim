#include "psim/platform/authz/policy.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::platform::authz {
bool all_scopes(const security::Principal& principal) {
  return !principal.tenant.empty() && principal.scopes.contains("*");
}

bool any_scope(const security::Principal& principal) {
  return !principal.tenant.empty() && std::ranges::any_of(principal.scopes, [](const auto& scope) {
    return !scope.empty() && scope != "site:" && scope != "zone:";
  });
}

ScopeFilter scope_filter(const security::Principal& principal) {
  ScopeFilter result;
  result.all = all_scopes(principal);
  for (const auto& scope : principal.scopes) {
    if (scope.starts_with("site:")) {
      if (scope.size() > 5) {
        result.sites.push_back(scope.substr(5));
      }
    } else if (scope.starts_with("zone:")) {
      if (scope.size() > 5) {
        result.zones.push_back(scope.substr(5));
      }
    } else if (!scope.empty() && scope != "*") {
      result.sites.push_back(scope);
      result.zones.push_back(scope);
    }
  }
  return result;
}

bool scoped(const security::Principal& principal, Resource resource) {
  if (principal.tenant.empty() || principal.tenant != resource.tenant) {
    return false;
  }
  return principal.scopes.contains("*")
         || (!resource.site.empty()
             && (principal.scopes.contains(std::string(resource.site))
                 || principal.scopes.contains("site:" + std::string(resource.site))))
         || (!resource.zone.empty()
             && (principal.scopes.contains(std::string(resource.zone))
                 || principal.scopes.contains("zone:" + std::string(resource.zone))));
}

bool covers_scopes(const security::Principal& principal, const security::Principal& required) {
  if (principal.tenant.empty() || principal.tenant != required.tenant) {
    return false;
  }
  if (all_scopes(principal)) {
    return true;
  }
  return std::ranges::all_of(required.scopes, [&](const auto& scope) {
    const std::string_view id(scope);
    if (scope == "*") {
      return false;
    }
    if (id.starts_with("site:")) {
      return scoped(principal, {required.tenant, id.substr(5)});
    }
    if (id.starts_with("zone:")) {
      return scoped(principal, {required.tenant, {}, id.substr(5)});
    }
    return scoped(principal, {required.tenant, id, id});
  });
}

Result<void> decide_role(const security::Principal& principal, std::string_view action,
                         std::chrono::system_clock::time_point now) {
  if (principal.subject.empty() || principal.tenant.empty() || principal.expires_at <= now) {
    return fail(ErrorCode::kAuthUnauthenticated, "current verified identity required");
  }
  static const auto kPolicies = nlohmann::json::parse(kPolicy);
  const auto found = kPolicies.find(std::string(action));
  if (found == kPolicies.end()) {
    return fail(ErrorCode::kAuthForbidden, "unknown action");
  }
  bool allowed = false;
  for (const auto& role : *found) {
    allowed = allowed || principal.roles.contains(role.get<std::string>());
  }
  if (!allowed) {
    return fail(ErrorCode::kAuthForbidden, "role does not allow action");
  }
  return {};
}

Result<void> decide_somewhere(const security::Principal& principal, std::string_view action,
                              std::chrono::system_clock::time_point now) {
  auto role = decide_role(principal, action, now);
  if (!role) {
    return role;
  }
  if (!any_scope(principal)) {
    return fail(ErrorCode::kAuthScopeDenied, "access scope required");
  }
  return {};
}

std::vector<std::string> actions(const security::Principal& principal, std::chrono::system_clock::time_point now) {
  static const auto kPolicies = nlohmann::json::parse(kPolicy);
  std::vector<std::string> result;
  for (const auto& [action, roles] : kPolicies.items()) {
    (void)roles;
    if (decide_role(principal, action, now)) {
      result.push_back(action);
    }
  }
  return result;
}

Result<void> decide(const security::Principal& principal, std::string_view action, Resource resource,
                    std::chrono::system_clock::time_point now) {
  auto role = decide_role(principal, action, now);
  if (!role) {
    return role;
  }
  if (action == "incident.types.read" && principal.tenant == resource.tenant && any_scope(principal)) {
    return {};
  }
  if (!scoped(principal, resource)) {
    return fail(ErrorCode::kAuthScopeDenied, "resource outside access scope");
  }
  return {};
}
}  // namespace psim::platform::authz
