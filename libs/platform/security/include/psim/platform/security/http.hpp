#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::security {
struct HttpOptions {
  TlsOptions tls{};
  std::chrono::milliseconds timeout{5000};
  std::size_t max_body_bytes = std::size_t{1024} * 1024;
  /// Explicit opt-in for isolated development fixtures; production uses HTTPS.
  bool allow_plaintext = false;
  /// Explicit credentials for the configured endpoint; redirects are never followed.
  std::string bearer_token{};
  /// Reject non-public addresses after DNS and connect only to the checked endpoints.
  bool public_network_only = false;
  /// Additional application headers; framing and authorization headers are reserved.
  std::map<std::string, std::string> headers{};
};

struct HttpResponse {
  unsigned int status = 0;
  std::string body{};
};

/// Fixed configured endpoint, bounded headers/body and one deadline including DNS; no redirects,
/// URL userinfo, fragments, token-selected URLs, cookies or automatic retries.
[[nodiscard]] bool public_address(std::string_view input);
[[nodiscard]] Result<void> validate_url(std::string_view url, bool allow_plaintext = false);
boost::asio::awaitable<Result<HttpResponse>> http_request(std::string url, HttpOptions options = {},
                                                          std::string body = {}, std::string content_type = {});
}  // namespace psim::platform::security
