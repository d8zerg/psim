#include "http.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "rules.hpp"

namespace psim::correlation::management {
using Code = p::ErrorCode;

namespace {
p::Result<std::uint64_t> number(std::string_view text) {
  std::uint64_t value = 0;
  const auto converted = std::from_chars(std::to_address(text.begin()), std::to_address(text.end()), value);
  if (converted.ec != std::errc{} || converted.ptr != std::to_address(text.end()) || std::to_string(value) != text) {
    return p::fail(Code::kCommonInvalidArgument, "canonical nonnegative integer required");
  }
  return value;
}

p::Result<std::string> unescape(std::string_view input) {
  std::string output;
  for (std::size_t i = 0; i < input.size(); ++i) {
    auto ch = input[i];
    if (ch == '%') {
      if (i + 2 >= input.size()) {
        return p::fail(Code::kCommonInvalidArgument, "invalid URL encoding");
      }
      auto hex = [](char value) -> unsigned int {
        if (value >= '0' && value <= '9') {
          return static_cast<unsigned int>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
          return static_cast<unsigned int>(value - 'a' + 10);
        }
        if (value >= 'A' && value <= 'F') {
          return static_cast<unsigned int>(value - 'A' + 10);
        }
        return 16;
      };
      const auto first = hex(input[i + 1]);
      const auto second = hex(input[i + 2]);
      if (first == 16 || second == 16) {
        return p::fail(Code::kCommonInvalidArgument, "invalid URL encoding");
      }
      ch = static_cast<char>((first * 16) + second);
      i += 2;
    } else if (ch == '+') {
      ch = ' ';
    }
    if (ch == '\0') {
      return p::fail(Code::kCommonInvalidArgument, "NUL in URL");
    }
    output += ch;
  }
  return output;
}
}  // namespace

Response Api::problem(const p::Error& error) {
  const auto& info = p::info(error.code());
  Response response(static_cast<http::status>(info.http_status), 11);
  response.set(http::field::content_type, "application/problem+json");
  response.body() =
      Json{{"type", "about:blank"}, {"title", info.title}, {"status", info.http_status}, {"code", error.name()}}.dump();
  return response;
}

asio::awaitable<p::Result<Response>> Api::handle(Request request, p::security::Principal principal) {
  try {
    co_return co_await route(std::move(request), std::move(principal));
  } catch (const std::exception&) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid rule request fields");
  }
}

asio::awaitable<p::Result<Response>> Api::route(Request request, p::security::Principal principal) {
  if (request.target().size() > 4096 || request.body().size() > 65536) {
    co_return p::fail(Code::kCommonInvalidArgument, "request size limit exceeded");
  }
  const std::string target(request.target());
  const auto question = target.find('?');
  const auto path = target.substr(0, question);
  Json query = Json::object();
  auto tail = question == std::string::npos ? std::string_view{} : std::string_view(target).substr(question + 1);
  while (!tail.empty()) {
    const auto amp = tail.find('&');
    const auto entry = tail.substr(0, amp);
    const auto equal = entry.find('=');
    if (equal == std::string_view::npos) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid query parameter");
    }
    auto name = unescape(entry.substr(0, equal));
    auto value = unescape(entry.substr(equal + 1));
    if (!name || !value) {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid query encoding");
    }
    if (*name == "states") {
      if (!query.contains(*name)) {
        query[*name] = Json::array();
      }
      query[*name].push_back(*value);
    } else {
      if (query.contains(*name)) {
        co_return p::fail(Code::kCommonInvalidArgument, "duplicate query parameter");
      }
      if (*name == "pageSize" || *name == "version") {
        auto parsed = number(*value);
        if (!parsed || *parsed > 4294967295ULL) {
          co_return p::fail(Code::kCommonInvalidArgument, "invalid numeric query parameter");
        }
        query[*name] = *parsed;
      } else {
        query[*name] = *value;
      }
    }
    if (amp == std::string_view::npos) {
      break;
    }
    tail.remove_prefix(amp + 1);
  }
  const bool post = request.method() == http::verb::post;
  if (!post && request.method() != http::verb::get) {
    co_return p::fail(Code::kCommonInvalidArgument, "GET or POST required");
  }
  if (post && !query.empty()) {
    co_return p::fail(Code::kCommonInvalidArgument, "unexpected query parameters");
  }
  const auto content_type = std::string(request[http::field::content_type]);
  std::string operation;
  Json body = query;
  if (post) {
    body = Json::object();
    if (!request.body().empty()) {
      body = Json::parse(request.body(), nullptr, false);
    }
  }
  if (path == "/v1/rules" && post) {
    const bool yaml = content_type.starts_with("application/yaml") || content_type.starts_with("text/yaml");
    if (!yaml && !content_type.starts_with("application/json")) {
      co_return p::fail(Code::kCommonInvalidArgument, "JSON or YAML rule required");
    }
    auto rule = rules::parse_document(request.body(), yaml);
    if (!rule) {
      co_return std::unexpected(rule.error());
    }
    body = Json::parse(p::stream::json(*rule));
    operation = "save";
  } else {
    if (post && !content_type.starts_with("application/json")) {
      co_return p::fail(Code::kCommonInvalidArgument, "JSON request required");
    }
    if (!body.is_object()) {
      co_return p::fail(Code::kCommonInvalidArgument, "request object required");
    }
    if (path == "/v1/rules" && !post) {
      operation = "list";
    } else if (path == "/v1/rule-sets/active" && !post) {
      operation = "active";
    } else if (path == "/v1/rule-sets:activate" && post) {
      operation = "activate";
    } else if (path.starts_with("/v1/dry-runs/") && !post) {
      if (body.contains("dryRunId")) {
        co_return p::fail(Code::kCommonInvalidArgument, "unexpected dry-run identifier");
      }
      body["dryRunId"] = path.substr(13);
      operation = "get-run";
    } else if (path.starts_with("/v1/rules/")) {
      const auto rest = path.substr(10);
      const auto colon = rest.find(':');
      const auto id = rest.substr(0, colon);
      if (body.contains("ruleId") && body.at("ruleId") != id) {
        co_return p::fail(Code::kCommonInvalidArgument, "path and body rule identifiers differ");
      }
      body["ruleId"] = id;
      if (!post && colon == std::string::npos) {
        operation = "get";
      } else if (post && colon != std::string::npos) {
        operation = rest.substr(colon + 1);
        if (operation != "validate" && operation != "archive" && operation != "dry-run") {
          operation.clear();
        }
      }
    }
  }
  if (operation.empty()) {
    co_return p::fail(Code::kCommonNotFound, "rule endpoint not found");
  }
  std::optional<std::uint64_t> expected;
  const std::string match(request[http::field::if_match]);
  if (!match.empty()) {
    if (match.size() < 3 || match.front() != '"' || match.back() != '"') {
      co_return p::fail(Code::kCommonInvalidArgument, "quoted If-Match version required");
    }
    auto parsed = number(std::string_view(match).substr(1, match.size() - 2));
    if (!parsed) {
      co_return std::unexpected(parsed.error());
    }
    expected = *parsed;
  }
  auto result = co_await repository_->request(std::move(principal), operation, std::move(body),
                                              std::string(request["Idempotency-Key"]), expected);
  if (!result) {
    co_return std::unexpected(result.error());
  }
  Response response(http::status::ok, 11);
  response.set(http::field::content_type, "application/json");
  response.body() = result->dump();
  if (result->contains("rule")) {
    response.set(http::field::etag, '"' + std::to_string(result->at("rule").at("version").get<std::uint32_t>()) + '"');
  }
  if (result->contains("ruleSet")) {
    response.set(http::field::etag, '"' + result->at("ruleSet").at("version").get<std::string>() + '"');
  }
  co_return response;
}
}  // namespace psim::correlation::management
