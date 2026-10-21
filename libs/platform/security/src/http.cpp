#include "psim/platform/security/http.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/tls.hpp"

namespace psim::platform::security {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

namespace {
constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);

struct Url {
  bool tls;
  std::string host;
  std::string authority;
  std::string port;
  std::string target;
};

Result<Url> parse_url(std::string_view url, bool allow_plaintext) {
  const bool tls = url.starts_with("https://");
  if (!tls && !(allow_plaintext && url.starts_with("http://"))) {
    return fail(ErrorCode::kCommonInvalidArgument, "HTTPS endpoint required");
  }
  if (url.size() > 2048 || url.find('#') != std::string_view::npos || url.find('@') != std::string_view::npos) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid HTTP endpoint");
  }
  for (const char c : url) {
    if (static_cast<unsigned char>(c) <= 32 || static_cast<unsigned char>(c) >= 127 || c == '\\') {
      return fail(ErrorCode::kCommonInvalidArgument, "invalid HTTP endpoint");
    }
  }
  url.remove_prefix(tls ? 8 : 7);
  const auto slash = url.find('/');
  const auto authority = url.substr(0, slash);
  const auto colon = authority.rfind(':');
  const bool ipv6 = authority.starts_with('[');
  const auto bracket = authority.find(']');
  if (ipv6
      && (bracket == std::string_view::npos || (bracket + 1 < authority.size() && authority[bracket + 1] != ':'))) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid HTTP endpoint");
  }
  const bool has_port = ipv6 ? bracket + 1 < authority.size() : colon != std::string_view::npos;
  const auto host = ipv6 ? authority.substr(1, bracket - 1) : authority.substr(0, has_port ? colon : authority.size());
  const auto port = has_port ? authority.substr(ipv6 ? bracket + 2 : colon + 1) : std::string_view(tls ? "443" : "80");
  unsigned int number = 0;
  const auto parsed = std::from_chars(port.begin(), port.end(), number);
  if (host.empty() || host.size() > 253 || parsed.ec != std::errc{} || parsed.ptr != port.end() || number == 0
      || number > 65535) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid HTTP endpoint");
  }
  for (const char c : host) {
    if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '.' && c != '-'
        && (!ipv6 || c != ':')) {
      return fail(ErrorCode::kCommonInvalidArgument, "invalid HTTP hostname");
    }
  }
  return Url{tls, std::string(host), std::string(authority), std::string(port),
             slash == std::string_view::npos ? "/" : std::string(url.substr(slash))};
}

template <typename Stream>
asio::awaitable<Result<HttpResponse>> exchange(Stream* stream, const Url* url, const HttpOptions* options,
                                               std::string body, std::string content_type) {
  http::request<http::string_body> request(body.empty() ? http::verb::get : http::verb::post, url->target, 11);
  request.set(http::field::host, url->authority);
  request.set(http::field::accept, "application/json");
  request.set(http::field::connection, "close");
  if (!options->bearer_token.empty()) {
    request.set(http::field::authorization, "Bearer " + options->bearer_token);
  }
  if (!body.empty()) {
    request.set(http::field::content_type, content_type);
    request.body() = std::move(body);
    request.prepare_payload();
  }
  if (auto [error, size] = co_await http::async_write(*stream, request, kAwait); error) {
    co_return fail(ErrorCode::kCommonUnavailable, "HTTP request failed");
  }
  beast::flat_buffer buffer(32768);
  http::response_parser<http::string_body> parser;
  parser.body_limit(options->max_body_bytes);
  parser.header_limit(16384);
  if (auto [error, size] = co_await http::async_read(*stream, buffer, parser, kAwait); error) {
    co_return fail(ErrorCode::kCommonUnavailable, "HTTP response failed or exceeds limit");
  }
  auto response = parser.release();
  co_return HttpResponse{response.result_int(), std::move(response.body())};
}

asio::awaitable<Result<HttpResponse>> request_once(Url url, HttpOptions options, std::string body,
                                                   std::string content_type) {
  auto executor = co_await asio::this_coro::executor;
  asio::ip::tcp::resolver resolver(executor);
  auto [error, endpoints] = co_await resolver.async_resolve(url.host, url.port, kAwait);
  if (error) {
    co_return fail(ErrorCode::kCommonUnavailable, "HTTP resolution failed");
  }
  if (!url.tls) {
    beast::tcp_stream stream(executor);
    if (auto [ec, endpoint] = co_await stream.async_connect(endpoints, kAwait); ec) {
      co_return fail(ErrorCode::kCommonUnavailable, "HTTP connection failed");
    }
    co_return co_await exchange(&stream, &url, &options, std::move(body), std::move(content_type));
  }
  auto context = tls_context(options.tls);
  if (!context) {
    co_return std::unexpected(context.error());
  }
  asio::ssl::stream<beast::tcp_stream> stream(executor, **context);
  auto configured = configure_hostname(stream.native_handle(), url.host);
  if (!configured) {
    co_return std::unexpected(configured.error());
  }
  if (auto [ec, endpoint] = co_await stream.next_layer().async_connect(endpoints, kAwait); ec) {
    co_return fail(ErrorCode::kCommonUnavailable, "HTTPS connection failed");
  }
  if (auto [ec] = co_await stream.async_handshake(asio::ssl::stream_base::client, kAwait); ec) {
    co_return fail(ErrorCode::kAuthUnauthenticated, "HTTPS trust verification failed");
  }
  auto response = co_await exchange(&stream, &url, &options, std::move(body), std::move(content_type));
  // Full message received; close the socket without waiting indefinitely for peer close_notify.
  boost::system::error_code ignored;
  (void)stream.next_layer().socket().close(ignored);
  co_return response;
}
}  // namespace

Result<void> validate_url(std::string_view url, bool allow_plaintext) {
  auto parsed = parse_url(url, allow_plaintext);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return {};
}

asio::awaitable<Result<HttpResponse>> http_request(std::string url, HttpOptions options, std::string body,
                                                   std::string content_type) {
  auto parsed = parse_url(url, options.allow_plaintext);
  if (!parsed) {
    co_return std::unexpected(parsed.error());
  }
  if (options.timeout <= std::chrono::milliseconds::zero() || options.timeout > std::chrono::seconds(60)
      || options.max_body_bytes == 0 || options.max_body_bytes > std::size_t{4} * 1024 * 1024
      || options.bearer_token.size() > 16384 || options.bearer_token.find_first_of("\r\n") != std::string::npos
      || body.size() > std::size_t{1024} * 1024 || content_type.find_first_of("\r\n") != std::string::npos) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "invalid HTTP request limits");
  }
  try {
    async::SystemClock clock;
    const auto timeout = options.timeout;
    auto result = co_await async::with_deadline(
        &clock, timeout,
        request_once(std::move(*parsed), std::move(options), std::move(body), std::move(content_type)));
    if (!result) {
      co_return std::unexpected(result.error());
    }
    co_return std::move(*result);
  } catch (const std::exception&) {
    co_return fail(ErrorCode::kCommonUnavailable, "HTTP operation cancelled or failed");
  }
}
}  // namespace psim::platform::security
