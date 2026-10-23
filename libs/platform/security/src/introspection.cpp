#include <boost/asio/as_tuple.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::platform::security {
namespace asio = boost::asio;
using Json = nlohmann::json;

struct JwksCache::Inspection {
  Result<Principal> result = fail(ErrorCode::kCommonUnavailable, "token introspection pending");
  std::chrono::system_clock::time_point checked{};
  bool pending = true;
  std::vector<std::weak_ptr<asio::steady_timer>> waiters;
};

namespace {
Result<std::set<std::string>> scopes(const Json& value) {
  if (!value.is_array() || value.size() > 10000) {
    return fail(ErrorCode::kAuthUnauthenticated, "invalid introspection scopes");
  }
  std::set<std::string> result;
  for (const auto& scope : value) {
    if (!scope.is_string() || scope.get_ref<const std::string&>().empty()
        || scope.get_ref<const std::string&>().size() > 512) {
      return fail(ErrorCode::kAuthUnauthenticated, "invalid introspection scope");
    }
    result.insert(scope.get<std::string>());
  }
  return result;
}

Result<Principal> resolve(const Principal& principal, const Json& value, const JwtOptions& options,
                          std::chrono::system_clock::time_point now) {
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  if (!value.is_object() || !value.value("active", false) || value.at("sub") != principal.subject
      || value.at("iss") != options.issuer || value.at("client_id") != principal.authorized_party
      || value.value("psim_tenant", "") != principal.tenant || !value.at("exp").is_number_integer()
      || value.at("exp").get<std::int64_t>() <= seconds
      || value.at("exp").get<std::int64_t>()
             != std::chrono::duration_cast<std::chrono::seconds>(principal.expires_at.time_since_epoch()).count()
      || value.value("psim_scope_set", "") != principal.scope_set) {
    return fail(ErrorCode::kAuthUnauthenticated, "access token revoked or identity changed");
  }
  const auto current_scopes = scopes(value.value("psim_scopes", Json::array()));
  const auto current_roles = scopes(value.value("resource_access", Json::object())
                                        .value(options.roles_client, Json::object())
                                        .value("roles", Json::array()));
  if (!current_scopes || !current_roles || *current_roles != principal.roles
      || (principal.scope_set.empty() && *current_scopes != principal.scopes)) {
    return fail(ErrorCode::kAuthUnauthenticated, "access claims changed; refresh required");
  }
  auto result = principal;
  result.scopes = *current_scopes;
  result.checked_until = std::min(principal.expires_at, now + std::chrono::seconds(1));
  return result;
}
}  // namespace

asio::awaitable<Result<Principal>> JwksCache::inspect(std::string token, Principal principal) {
  const auto now = now_();
  auto found = inspections_.find(token);
  if (found != inspections_.end()) {
    const auto entry = found->second;
    if (entry->pending) {
      if (entry->waiters.size() >= 1024) {
        co_return fail(ErrorCode::kCommonUnavailable, "token introspection waiter capacity reached");
      }
      auto waiter = std::make_shared<asio::steady_timer>(co_await asio::this_coro::executor);
      waiter->expires_after(std::chrono::seconds(2));
      entry->waiters.push_back(waiter);
      co_await waiter->async_wait(asio::as_tuple(asio::use_awaitable));
      if (entry->pending) {
        co_return fail(ErrorCode::kCommonUnavailable, "token introspection not complete");
      }
    }
    if (now_() >= entry->checked && now_() - entry->checked < std::chrono::milliseconds(500)) {
      co_return entry->result;
    }
    found = inspections_.find(token);
    if (found != inspections_.end() && found->second == entry) {
      inspections_.erase(found);
    }
  }
  std::erase_if(inspections_, [now](const auto& item) {
    return !item.second->pending && now - item.second->checked >= std::chrono::milliseconds(500);
  });
  if (inspections_.size() >= 1024) {
    co_return fail(ErrorCode::kCommonUnavailable, "token introspection capacity reached");
  }
  const auto entry = std::make_shared<Inspection>();
  inspections_.emplace(token, entry);

  struct Complete {
    explicit Complete(std::shared_ptr<Inspection> value) : entry(std::move(value)) {}

    Complete(const Complete&) = delete;
    Complete& operator=(const Complete&) = delete;
    Complete(Complete&&) = delete;
    Complete& operator=(Complete&&) = delete;
    std::shared_ptr<Inspection> entry;

    ~Complete() noexcept {
      entry->pending = false;
      for (const auto& weak : entry->waiters) {
        if (auto waiter = weak.lock()) {
          try {
            waiter->cancel();
          } catch (const std::exception&) {
            // A failed cancellation leaves the bounded waiter timeout in effect.
            continue;
          }
        }
      }
    }
  };

  const Complete complete{entry};

  try {
    auto body = co_await inspect_(std::move(token));
    entry->checked = now_();
    entry->result = body ? resolve(principal, Json::parse(*body), options_.jwt, entry->checked)
                         : Result<Principal>(std::unexpected(body.error()));
  } catch (const std::exception&) {
    entry->result = fail(ErrorCode::kCommonUnavailable, "invalid token introspection response");
    entry->checked = now_();
  }
  co_return entry->result;
}
}  // namespace psim::platform::security
