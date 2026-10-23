#include "psim/platform/identity/directory.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <expected>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::platform::identity {
namespace p = psim::platform;

namespace {
std::string form(std::string_view input) {
  constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string output;
  for (const char byte : input) {
    const auto value = static_cast<unsigned char>(byte);
    if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9')
        || value == '-' || value == '_' || value == '.') {
      output += static_cast<char>(value);
    } else {
      output += '%';
      output += kHex[value >> 4];
      output += kHex[value & 15];
    }
  }
  return output;
}
}  // namespace

boost::asio::awaitable<p::Result<p::security::Principal>> Directory::lookup(std::string subject) const {
  if (!p::stream::uuid(subject)) {
    co_return p::fail(p::ErrorCode::kIncidentAssigneeInvalid, "invalid recipient");
  }
  if (options_.base.empty() || options_.client.empty() || options_.secret.empty()
      || !p::stream::uuid(options_.roles_client)) {
    co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity directory not configured");
  }
  try {
    auto token = co_await p::security::http_request(
        options_.base + "/realms/psim/protocol/openid-connect/token", options_.http,
        "grant_type=client_credentials&client_id=" + form(options_.client) + "&client_secret=" + form(options_.secret),
        "application/x-www-form-urlencoded");
    if (!token || token->status != 200) {
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity directory authentication unavailable");
    }
    auto http = options_.http;
    http.bearer_token = nlohmann::json::parse(token->body).at("access_token").get<std::string>();
    auto user = co_await p::security::http_request(options_.base + "/admin/realms/psim/users/" + subject, http);
    if (user && user->status == 404) {
      co_return p::fail(p::ErrorCode::kIncidentAssigneeInvalid, "recipient does not exist");
    }
    if (!user || user->status != 200) {
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "identity directory unavailable");
    }
    const auto profile = nlohmann::json::parse(user->body);
    if (!profile.value("enabled", false) || profile.at("id") != subject) {
      co_return p::fail(p::ErrorCode::kIncidentAssigneeInvalid, "recipient inactive");
    }
    auto roles =
        co_await p::security::http_request(options_.base + "/admin/realms/psim/users/" + subject
                                               + "/role-mappings/clients/" + options_.roles_client + "/composite",
                                           http);
    if (!roles || roles->status != 200) {
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "recipient roles unavailable");
    }
    p::security::Principal result{.subject = subject,
                                  .expires_at = std::chrono::system_clock::now() + std::chrono::seconds(10)};
    const auto attributes = profile.value("attributes", nlohmann::json::object());
    if (!attributes.contains("psim_tenant") || attributes["psim_tenant"].size() != 1) {
      co_return p::fail(p::ErrorCode::kIncidentAssigneeInvalid, "recipient tenant unavailable");
    }
    result.tenant = attributes["psim_tenant"][0].get<std::string>();
    for (const auto& scope : attributes.value("psim_scopes", nlohmann::json::array())) {
      result.scopes.insert(scope.get<std::string>());
    }
    auto groups = co_await p::security::http_request(
        options_.base + "/admin/realms/psim/users/" + subject + "/groups?briefRepresentation=false&max=101", http);
    if (!groups || groups->status != 200) {
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "recipient groups unavailable");
    }
    auto pending = nlohmann::json::parse(groups->body).get<std::vector<nlohmann::json>>();
    std::set<std::string> visited;
    while (!pending.empty()) {
      auto group = std::move(pending.back());
      pending.pop_back();
      const auto id = group.at("id").get<std::string>();
      if (!visited.insert(id).second) {
        continue;
      }
      if (visited.size() > 100) {
        co_return p::fail(p::ErrorCode::kCommonUnavailable, "recipient group capacity exceeded");
      }
      for (const auto& scope :
           group.value("attributes", nlohmann::json::object()).value("psim_scopes", nlohmann::json::array())) {
        result.scopes.insert(scope.get<std::string>());
      }
      const auto parent = group.value("parentId", std::string{});
      if (!parent.empty() && !visited.contains(parent)) {
        if (!p::stream::uuid(parent)) {
          co_return p::fail(p::ErrorCode::kCommonUnavailable, "invalid recipient group");
        }
        auto ancestor = co_await p::security::http_request(options_.base + "/admin/realms/psim/groups/" + parent, http);
        if (!ancestor || ancestor->status != 200) {
          co_return p::fail(p::ErrorCode::kCommonUnavailable, "recipient group unavailable");
        }
        pending.push_back(nlohmann::json::parse(ancestor->body));
      }
      if (result.scopes.size() > 10000) {
        co_return p::fail(p::ErrorCode::kCommonUnavailable, "recipient scope capacity exceeded");
      }
    }
    for (const auto& role : nlohmann::json::parse(roles->body)) {
      result.roles.insert(role.at("name").get<std::string>());
    }
    co_return result;
  } catch (const std::exception&) {
    co_return p::fail(p::ErrorCode::kCommonUnavailable, "invalid identity directory response");
  }
}
}  // namespace psim::platform::identity
