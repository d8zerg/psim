#include "psim/platform/kafka/schema_registry.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::kafka {

namespace beast = boost::beast;
namespace http = boost::beast::http;
using asio::ip::tcp;

namespace {

constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);
constexpr int kHttp11 = 11;

// Subjects are path segments: "/" in "psim/common/v1/envelope.proto" must be escaped.
std::string escape(std::string_view segment) {
  static constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string out;
  for (const char c : segment) {
    const auto byte = static_cast<unsigned char>(c);
    if ((std::isalnum(byte) != 0) || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(c);
    } else {
      out.push_back('%');
      out.push_back(kHex.at(byte >> 4U));
      out.push_back(kHex.at(byte & 0x0FU));
    }
  }
  return out;
}

std::unexpected<Error> unavailable(std::string_view what, std::string_view detail) {
  return fail(ErrorCode::kCommonUnavailable, "schema registry " + std::string(what) + ": " + std::string(detail));
}

}  // namespace

SchemaRegistry::SchemaRegistry(std::string host, std::string port, std::string base, std::chrono::milliseconds timeout)
    : host_(std::move(host)), port_(std::move(port)), base_(std::move(base)), timeout_(timeout) {}

Result<SchemaRegistry> SchemaRegistry::create(const SchemaRegistryOptions& options) {
  constexpr std::string_view kScheme = "http://";
  std::string_view url = options.url;
  if (!url.starts_with(kScheme)) {
    return fail(ErrorCode::kCommonInvalidArgument, "schema registry url must start with http:// (TLS: step 2.6)");
  }
  url.remove_prefix(kScheme.size());
  const auto slash = url.find('/');
  const std::string_view authority = url.substr(0, slash);
  std::string base(slash == std::string_view::npos ? std::string_view{} : url.substr(slash));
  while (base.ends_with('/')) {
    base.pop_back();
  }
  const auto colon = authority.rfind(':');
  std::string host(authority.substr(0, colon));
  std::string port(colon == std::string_view::npos ? "80" : authority.substr(colon + 1));
  if (host.empty() || port.empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "schema registry url has no host: " + options.url);
  }
  return SchemaRegistry(std::move(host), std::move(port), std::move(base), options.timeout);
}

asio::awaitable<Result<std::string>> SchemaRegistry::get(std::string path) {
  auto executor = co_await asio::this_coro::executor;
  tcp::resolver resolver(executor);
  auto [resolve_ec, endpoints] = co_await resolver.async_resolve(host_, port_, kAwait);
  if (resolve_ec) {
    co_return unavailable("resolve " + host_, resolve_ec.message());
  }
  beast::tcp_stream stream(executor);
  stream.expires_after(timeout_);
  if (auto [ec, endpoint] = co_await stream.async_connect(endpoints, kAwait); ec) {
    co_return unavailable("connect " + host_ + ":" + port_, ec.message());
  }
  http::request<http::string_body> request(http::verb::get, base_ + path, kHttp11);
  request.set(http::field::host, host_);
  request.set(http::field::accept, "application/vnd.schemaregistry.v1+json, application/json");
  if (auto [ec, bytes] = co_await http::async_write(stream, request, kAwait); ec) {
    co_return unavailable("request", ec.message());
  }
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  if (auto [ec, bytes] = co_await http::async_read(stream, buffer, response, kAwait); ec) {
    co_return unavailable("response", ec.message());
  }
  boost::system::error_code ignored;
  std::ignore = stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
  if (response.result() == http::status::not_found) {
    co_return fail(ErrorCode::kCommonNotFound, "schema registry: " + path + " not found");
  }
  if (response.result() != http::status::ok) {
    co_return unavailable(path, "HTTP " + std::to_string(response.result_int()));
  }
  co_return std::move(response.body());
}

asio::awaitable<Result<std::int32_t>> SchemaRegistry::latest_id(std::string subject) {
  auto body = co_await get("/subjects/" + escape(subject) + "/versions/latest");
  if (!body) {
    co_return std::unexpected(std::move(body.error()));
  }
  const auto json = nlohmann::json::parse(*body, nullptr, false);
  if (json.is_discarded() || !json.contains("id") || !json["id"].is_number_integer()) {
    co_return unavailable("subject " + subject, "unexpected response");
  }
  const auto id = json["id"].get<std::int32_t>();
  if (json.contains("schema") && json["schema"].is_string()) {
    schemas_.emplace(id, json["schema"].get<std::string>());
  }
  co_return id;
}

asio::awaitable<Result<std::string>> SchemaRegistry::schema(std::int32_t id) {
  if (const auto it = schemas_.find(id); it != schemas_.end()) {
    co_return it->second;
  }
  auto body = co_await get("/schemas/ids/" + std::to_string(id));
  if (!body) {
    co_return std::unexpected(std::move(body.error()));
  }
  const auto json = nlohmann::json::parse(*body, nullptr, false);
  if (json.is_discarded() || !json.contains("schema") || !json["schema"].is_string()) {
    co_return unavailable("schema " + std::to_string(id), "unexpected response");
  }
  co_return schemas_.emplace(id, json["schema"].get<std::string>()).first->second;
}

}  // namespace psim::platform::kafka
