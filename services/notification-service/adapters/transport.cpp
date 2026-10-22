#include "transport.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/types.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::notification_service {
namespace {
constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);
using Stream = asio::ssl::stream<boost::beast::tcp_stream>;

const unsigned char* byte_data(std::string_view value) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL reads the unsigned byte representation.
  return reinterpret_cast<const unsigned char*>(value.data());
}

asio::awaitable<int> reply(Stream* stream, std::string* buffer) {
  for (int lines = 0; lines < 32; ++lines) {
    auto [error, size] = co_await asio::async_read_until(*stream, asio::dynamic_buffer(*buffer, 16384), "\r\n", kAwait);
    if (error || size < 5 || size > 1000) {
      co_return 0;
    }
    const auto line = buffer->substr(0, size);
    buffer->erase(0, size);
    int code = 0;
    const auto parsed =
        std::from_chars(std::string_view(line).substr(0, 3).begin(), std::string_view(line).substr(0, 3).end(), code);
    if (parsed.ec != std::errc{} || code < 200 || code > 599) {
      co_return 0;
    }
    if (line[3] == ' ') {
      co_return code;
    }
    if (line[3] != '-') {
      co_return 0;
    }
  }
  co_return 0;
}

asio::awaitable<bool> write(Stream* stream, std::string bytes) {
  auto [error, size] = co_await asio::async_write(*stream, asio::buffer(bytes), kAwait);
  (void)size;
  co_return !error;
}
}  // namespace

bool valid_email(std::string_view value) {
  if (value.empty() || value.size() > 254 || value.front() == '.' || value.back() == '.') {
    return false;
  }
  const auto at = value.find('@');
  if (at == 0 || at == std::string_view::npos || at + 1 == value.size()
      || value.find('@', at + 1) != std::string_view::npos) {
    return false;
  }
  return std::ranges::all_of(value, [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '@' || c == '.'
           || c == '_' || c == '+' || c == '-';
  });
}

std::string signature(std::string_view secret, std::string_view payload) {
  const std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr), EVP_MAC_free);
  const std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> context(mac ? EVP_MAC_CTX_new(mac.get()) : nullptr,
                                                                          EVP_MAC_CTX_free);
  auto digest = std::to_array("SHA256");
  std::array<OSSL_PARAM, 2> params{OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest.data(), 0),
                                   OSSL_PARAM_construct_end()};
  std::array<unsigned char, 32> bytes{};
  std::size_t size = 0;
  if (!context || EVP_MAC_init(context.get(), byte_data(secret), secret.size(), params.data()) != 1
      || EVP_MAC_update(context.get(), byte_data(payload), payload.size()) != 1
      || EVP_MAC_final(context.get(), bytes.data(), &size, bytes.size()) != 1) {
    return {};
  }
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string result;
  for (const auto byte : bytes) {
    result += kHex[byte >> 4];
    result += kHex[byte & 15];
  }
  return result;
}

p::Result<void> Transport::validate_webhook(std::string_view url) const {
  auto valid = p::security::validate_url(url);
  if (!valid || !options_.webhook_urls.contains(std::string(url)) || options_.hmac_secret.size() < 32) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "webhook outside configured allowlist");
  }
  return {};
}

asio::awaitable<p::Result<void>> Transport::check_webhook(std::string url) const {
  auto valid = validate_webhook(url);
  if (!valid) {
    co_return valid;
  }
  if (url == options_.fixture_webhook_url) {
    co_return p::Result<void>{};
  }
  const auto authority = url.substr(8, url.find('/', 8) - 8);
  const bool ipv6 = authority.starts_with('[');
  const auto host = ipv6 ? authority.substr(1, authority.find(']') - 1) : authority.substr(0, authority.find(':'));
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): the closure remains alive until awaited.
  const auto resolve = [&host]() -> asio::awaitable<p::Result<void>> {
    const auto executor = co_await asio::this_coro::executor;
    asio::ip::tcp::resolver resolver(executor);
    auto [error, endpoints] = co_await resolver.async_resolve(host, "443", kAwait);
    if (error) {
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "webhook destination unavailable");
    }
    for (const auto& endpoint : endpoints) {
      if (!p::security::public_address(endpoint.endpoint().address().to_string())) {
        co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "webhook destination forbidden");
      }
    }
    co_return p::Result<void>{};
  };
  p::async::SystemClock clock;
  auto result = co_await p::async::with_deadline(&clock, options_.timeout, resolve());
  if (!result) {
    co_return std::unexpected(result.error());
  }
  co_return *result;
}

asio::awaitable<p::Result<Receipt>> Transport::smtp(std::string id, std::string recipient, std::string payload,
                                                    bool* ambiguous) {
  const auto executor = co_await asio::this_coro::executor;
  asio::ip::tcp::resolver resolver(executor);
  auto [error, endpoints] = co_await resolver.async_resolve(options_.smtp_host, options_.smtp_port, kAwait);
  if (error) {
    co_return Receipt{Outcome::kRetry};
  }
  auto tls = p::security::tls_context({.ca_file = options_.ca_file, .require_client_certificate = false});
  if (!tls) {
    co_return Receipt{Outcome::kFailed};
  }
  Stream stream(executor, **tls);
  auto hostname = p::security::configure_hostname(stream.native_handle(), options_.smtp_host);
  if (!hostname) {
    co_return Receipt{Outcome::kFailed};
  }
  auto [connect_error, endpoint] = co_await stream.next_layer().async_connect(endpoints, kAwait);
  (void)endpoint;
  if (connect_error) {
    co_return Receipt{Outcome::kRetry};
  }
  auto [handshake] = co_await stream.async_handshake(asio::ssl::stream_base::client, kAwait);
  if (handshake) {
    co_return Receipt{Outcome::kFailed};
  }
  std::string buffer;
  if (co_await reply(&stream, &buffer) != 220) {
    co_return Receipt{Outcome::kRetry};
  }
  for (const auto& command : std::vector<std::string>{
           "EHLO psim.invalid\r\n", "MAIL FROM:<" + options_.smtp_from + ">\r\n", "RCPT TO:<" + recipient + ">\r\n"}) {
    if (!co_await write(&stream, command)) {
      co_return Receipt{Outcome::kRetry};
    }
    const auto code = co_await reply(&stream, &buffer);
    if (code < 200 || code >= 300) {
      co_return Receipt{code >= 500 ? Outcome::kFailed : Outcome::kRetry};
    }
  }
  if (!co_await write(&stream, "DATA\r\n")) {
    co_return Receipt{Outcome::kRetry};
  }
  const auto ready = co_await reply(&stream, &buffer);
  if (ready != 354) {
    co_return Receipt{ready >= 500 ? Outcome::kFailed : Outcome::kRetry};
  }
  const auto notification = Json::parse(payload);
  const auto data = notification.value("data", Json::object());
  std::string text = notification.value("template", std::string("Open-Source PSIM Platform notification"));
  if (data.contains("firstTemplate")) {
    text.append("\r\n\r\n").append(data.at("firstTemplate").get<std::string>());
  }
  auto incident = notification.value("incidentId", std::string{});
  if (incident.empty()) {
    incident = data.value("firstIncidentId", std::string{});
  }
  if (!incident.empty()) {
    text.append("\r\n\r\nIncident: ").append(incident);
  }
  if (data.contains("siteId")) {
    text.append("\r\nSite: ").append(data.at("siteId").get<std::string>());
  }
  if (data.contains("zoneId") && !data.at("zoneId").get<std::string>().empty()) {
    text.append("\r\nZone: ").append(data.at("zoneId").get<std::string>());
  }
  payload = std::move(text);
  // Any failure from the first DATA byte until the final reply can hide SMTP acceptance.
  *ambiguous = true;
  std::vector<unsigned char> encoded((4 * ((payload.size() + 2) / 3)) + 1);
  const auto encoded_size = EVP_EncodeBlock(encoded.data(), byte_data(payload), static_cast<int>(payload.size()));
  if (encoded_size <= 0) {
    co_return Receipt{Outcome::kFailed};
  }
  std::string content;
  content.reserve(encoded.size() + ((encoded.size() / 76) * 2));
  for (std::size_t offset = 0; std::cmp_less(offset, encoded_size); offset += 76) {
    const auto size = std::min<std::size_t>(76, static_cast<std::size_t>(encoded_size) - offset);
    for (std::size_t index = offset; index < offset + size; ++index) {
      content += static_cast<char>(encoded[index]);
    }
    content += "\r\n";
  }
  std::string message = "From: <";
  message.append(options_.smtp_from)
      .append(">\r\nTo: <")
      .append(recipient)
      .append(">\r\nMessage-ID: <")
      .append(id)
      .append(
          "@psim.invalid>\r\nSubject: Open-Source PSIM Platform notification\r\nMIME-Version: 1.0\r\nContent-Type: "
          "text/plain; charset=utf-8\r\nContent-Transfer-Encoding: base64\r\n\r\n")
      .append(content)
      .append(".\r\n");
  if (!co_await write(&stream, message)) {
    co_return Receipt{Outcome::kUnknown};
  }
  const auto accepted = co_await reply(&stream, &buffer);
  if (accepted == 250) {
    *ambiguous = false;
    co_return Receipt{Outcome::kAccepted, id};
  }
  if (accepted >= 400) {
    *ambiguous = false;
    co_return Receipt{accepted >= 500 ? Outcome::kFailed : Outcome::kRetry};
  }
  co_return Receipt{Outcome::kUnknown};
}

asio::awaitable<p::Result<Receipt>> Transport::send(int channel, std::string id, std::string destination,
                                                    std::string payload) {
  if (payload.size() > 65536) {
    co_return Receipt{Outcome::kFailed};
  }
  if (channel == 2) {
    if (!valid_email(destination) || !valid_email(options_.smtp_from) || options_.smtp_host.empty()) {
      co_return Receipt{Outcome::kFailed};
    }
    bool ambiguous = false;
    try {
      p::async::SystemClock clock;
      auto result =
          co_await p::async::with_deadline(&clock, options_.timeout, smtp(id, destination, payload, &ambiguous));
      if (!result) {
        co_return Receipt{ambiguous ? Outcome::kUnknown : Outcome::kRetry};
      }
      co_return std::move(*result);
    } catch (const std::exception&) {
      co_return Receipt{ambiguous ? Outcome::kUnknown : Outcome::kRetry};
    }
  }
  if (channel != 3 || !validate_webhook(destination)) {
    co_return Receipt{Outcome::kFailed};
  }
  p::security::HttpOptions options{.tls = {.ca_file = options_.ca_file, .require_client_certificate = false},
                                   .timeout = options_.timeout,
                                   .max_body_bytes = 4096};
  options.public_network_only = destination != options_.fixture_webhook_url;
  const auto timestamp = std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  options.headers = {
      {"Idempotency-Key", id},
      {"X-PSIM-Timestamp", timestamp},
      {"X-PSIM-Signature", "sha256=" + signature(options_.hmac_secret, timestamp + "." + id + "." + payload)}};
  auto response = co_await p::security::http_request(destination, options, payload, "application/json");
  if (!response) {
    co_return Receipt{response.error().code() == p::ErrorCode::kAuthForbidden
                              || response.error().code() == p::ErrorCode::kAuthUnauthenticated
                          ? Outcome::kFailed
                          : Outcome::kRetryUnknown};
  }
  if (response->status == 429 || response->status >= 500) {
    co_return Receipt{Outcome::kRetry};
  }
  if (response->status < 200 || response->status >= 300) {
    co_return Receipt{Outcome::kFailed};
  }
  try {
    const auto body = Json::parse(response->body);
    const auto receipt = body.value("receiptId", std::string{});
    if (body.value("notificationId", std::string{}) == id && body.value("accepted", false) && !receipt.empty()
        && receipt.size() <= 200) {
      co_return Receipt{Outcome::kAccepted, receipt};
    }
  } catch (const std::exception&) {
    co_return Receipt{Outcome::kRetryUnknown};
  }
  // Repeating the same id and immutable body must return the original receipt at the receiver.
  co_return Receipt{Outcome::kRetryUnknown};
}
}  // namespace psim::notification_service
