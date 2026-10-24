#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::security {
struct JwtOptions {
  std::string issuer{};
  std::string audience{};
  std::string roles_client = "psim";
  std::chrono::seconds clock_skew{30};
  std::chrono::seconds max_lifetime{300};
};

struct Principal {
  std::string subject{};
  std::string tenant{};
  std::string authorized_party{};
  std::set<std::string> roles{};
  std::set<std::string> scopes{};
  std::chrono::system_clock::time_point expires_at{};
  std::string scope_set{};
  std::chrono::system_clock::time_point checked_until{};
};

/// Canonical identity and effective access scope; excludes token lifetime and cache timestamps.
[[nodiscard]] std::string authorization_context(const Principal& principal);

/// A bounded, immutable JWKS snapshot. Duplicate kids, private keys and malformed supported keys
/// fail the entire replacement; RSA >= 2048, ES256 P-256, signatures through OpenSSL EVP only.
class JwtVerifier {
 public:
  [[nodiscard]] static Result<JwtVerifier> create(JwtOptions options, std::string_view jwks);
  [[nodiscard]] Result<Principal> verify(std::string_view token, std::chrono::system_clock::time_point now) const;
  [[nodiscard]] Result<bool> knows_key(std::string_view token) const;

 private:
  struct State;

  explicit JwtVerifier(std::shared_ptr<const State> state) : state_(std::move(state)) {}

  std::shared_ptr<const State> state_;
};

struct JwksOptions {
  JwtOptions jwt{};
  std::string url{};
  HttpOptions http{};
  std::chrono::seconds refresh_interval{60};
  std::chrono::seconds max_stale{300};
  std::chrono::seconds unknown_key_cooldown{5};
  std::string introspection_url{};
  std::string introspection_client{};
  std::string introspection_secret{};
};

/// Shard-owned cache. Known keys survive transient IdP outages for max_stale; removed keys disappear
/// on successful replacement. Unknown kids trigger a throttled fetch, never a URL from the token.
class JwksCache {
 public:
  using Fetch = std::function<boost::asio::awaitable<Result<std::string>>()>;
  using Now = std::function<std::chrono::system_clock::time_point()>;
  using Inspect = std::function<boost::asio::awaitable<Result<std::string>>(std::string)>;
  [[nodiscard]] static Result<std::unique_ptr<JwksCache>> create(JwksOptions options, Fetch fetch = {}, Now now = {},
                                                                 Inspect inspect = {});
  boost::asio::awaitable<Result<Principal>> verify(std::string token);
  boost::asio::awaitable<Result<void>> refresh();

 private:
  struct Inspection;
  JwksCache(JwksOptions options, Fetch fetch, Now now, Inspect inspect);
  boost::asio::awaitable<Result<Principal>> inspect(std::string token, Principal principal);
  JwksOptions options_;
  Fetch fetch_;
  Now now_;
  Inspect inspect_;
  std::map<std::string, std::shared_ptr<Inspection>> inspections_;
  std::unique_ptr<JwtVerifier> verifier_;
  std::chrono::system_clock::time_point loaded_{};
  std::chrono::system_clock::time_point attempted_{};
  bool attempted_once_ = false;
  bool refreshing_ = false;
};

/// Match a verified service URI SAN against an explicit allowlist. Service client credentials
/// additionally bind azp to that service; delegated user tokens preserve the user principal.
[[nodiscard]] Result<void> authenticate_service(const Peer& peer, const Principal& token,
                                                const std::set<std::string>& allowed_services, bool delegated_user,
                                                std::chrono::system_clock::time_point now);
}  // namespace psim::platform::security
