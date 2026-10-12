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
#include "psim/platform/security/http.hpp"

namespace psim::platform::kafka {

namespace {

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

SchemaRegistry::SchemaRegistry(std::string base, security::HttpOptions options)
    : base_(std::move(base)), options_(std::move(options)) {}

Result<SchemaRegistry> SchemaRegistry::create(const SchemaRegistryOptions& options) {
  auto valid = security::validate_url(options.url, options.http.allow_plaintext);
  if (!valid || options.timeout <= std::chrono::milliseconds::zero()) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid schema registry endpoint or timeout");
  }
  auto base = options.url;
  while (base.ends_with('/')) {
    base.pop_back();
  }
  auto http_options = options.http;
  http_options.timeout = options.timeout;
  return SchemaRegistry(std::move(base), std::move(http_options));
}

asio::awaitable<Result<std::string>> SchemaRegistry::get(std::string path) {
  auto response = co_await security::http_request(base_ + path, options_);
  if (!response) {
    co_return std::unexpected(response.error());
  }
  if (response->status == 404) {
    co_return fail(ErrorCode::kCommonNotFound, "schema registry entry not found");
  }
  if (response->status != 200) {
    co_return unavailable("request", "HTTP " + std::to_string(response->status));
  }
  co_return std::move(response->body);
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
