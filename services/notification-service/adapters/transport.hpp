#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <set>

#include "psim/platform/security/http.hpp"

namespace psim::notification_service {
namespace p = platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
enum class Outcome : std::uint8_t { kAccepted, kRetry, kFailed, kUnknown, kRetryUnknown };

struct Receipt {
  Outcome outcome = Outcome::kRetry;
  std::string id{};
};

struct TransportOptions {
  std::string smtp_host{}, smtp_port = "465", smtp_from{}, ca_file{}, hmac_secret{};
  std::set<std::string> webhook_urls{};
  // Only an isolated development fixture may use a private destination.
  std::string fixture_webhook_url{};
  std::chrono::milliseconds timeout{5000};
};

[[nodiscard]] bool valid_email(std::string_view value);
[[nodiscard]] std::string signature(std::string_view secret, std::string_view payload);

class Transport {
 public:
  explicit Transport(TransportOptions options) : options_(std::move(options)) {}

  [[nodiscard]] p::Result<void> validate_webhook(std::string_view url) const;
  asio::awaitable<p::Result<void>> check_webhook(std::string url) const;
  asio::awaitable<p::Result<Receipt>> send(int channel, std::string id, std::string destination, std::string payload);

 private:
  asio::awaitable<p::Result<Receipt>> smtp(std::string id, std::string recipient, std::string payload, bool* ambiguous);
  TransportOptions options_;
};
}  // namespace psim::notification_service
