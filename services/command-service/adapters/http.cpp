#include "http.hpp"

#include <google/protobuf/util/json_util.h>
#include <psim/api/v1/commands.pb.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "limits.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::command_service {
namespace {
std::string unquote(std::string_view input) {
  std::string output;
  for (std::size_t index = 0; index < input.size(); ++index) {
    if (input[index] == '%') {
      unsigned int value = 0;
      if (index + 2 >= input.size()) {
        throw std::invalid_argument("query");
      }
      const auto parsed =
          std::from_chars(input.substr(index + 1, 2).begin(), input.substr(index + 1, 2).end(), value, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != input.substr(index + 1, 2).end() || value == 0) {
        throw std::invalid_argument("query");
      }
      output += static_cast<char>(value);
      index += 2;
    } else {
      output += input[index] == '+' ? ' ' : input[index];
    }
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
    const std::string target(request.target());
    const auto question = target.find('?');
    const auto path = target.substr(0, question);
    const bool read = request.method() == http::verb::get;
    if (!read && request.method() != http::verb::post) {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown method");
    }
    Json body = Json::object();
    std::string operation;
    std::string id;
    if (read) {
      auto rest = question == std::string::npos ? std::string_view{} : std::string_view(target).substr(question + 1);
      while (!rest.empty()) {
        const auto amp = rest.find('&');
        const auto pair = rest.substr(0, amp);
        const auto equal = pair.find('=');
        if (equal == std::string_view::npos) {
          throw std::invalid_argument("query");
        }
        auto name = unquote(pair.substr(0, equal));
        auto value = unquote(pair.substr(equal + 1));
        if (name == "states") {
          if (!body.contains(name)) {
            body[name] = Json::array();
          }
          body[name].push_back(value);
        } else {
          if (body.contains(name)) {
            throw std::invalid_argument("duplicate query");
          }
          if (name == "pageSize") {
            int size = 0;
            const auto parsed = std::from_chars(std::string_view(value).begin(), std::string_view(value).end(), size);
            if (parsed.ec != std::errc{} || parsed.ptr != std::string_view(value).end()) {
              throw std::invalid_argument("page size");
            }
            body[name] = size;
          } else {
            body[name] = value;
          }
        }
        rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
      }
    } else {
      if (question != std::string::npos || request[http::field::content_type] != "application/json") {
        throw std::invalid_argument("JSON required");
      }
      body = request.body().empty() ? Json::object() : Json::parse(request.body());
      if (!body.is_object()) {
        throw std::invalid_argument("object required");
      }
    }
    if (path == "/v1/commands") {
      operation = read ? "list" : "request";
    } else if (path.starts_with("/v1/commands/")) {
      const auto tail = path.substr(13);
      const auto colon = tail.find(':');
      id = tail.substr(0, colon);
      if (read && colon == std::string::npos) {
        operation = "get";
      } else if (!read && colon != std::string::npos) {
        operation = tail.substr(colon + 1);
        if (operation != "approve" && operation != "reject" && operation != "cancel") {
          co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown action");
        }
      }
    }
    const std::map<std::string, std::string> types = {
        {"request", "RequestCommandRequest"}, {"get", "GetCommandRequest"},       {"list", "ListCommandsRequest"},
        {"approve", "ApproveCommandRequest"}, {"reject", "RejectCommandRequest"}, {"cancel", "CancelCommandRequest"}};
    if (!types.contains(operation)) {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown endpoint");
    }
    if (!id.empty()) {
      if (body.contains("commandId") || body.contains("command_id")) {
        throw std::invalid_argument("path identity must not be in body");
      }
      body["commandId"] = id;
    }
    const auto* descriptor =
        google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName("psim.api.v1." + types.at(operation));
    const std::unique_ptr<google::protobuf::Message> parsed(
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor)->New());
    if (!google::protobuf::util::JsonStringToMessage(body.dump(), parsed.get()).ok()) {
      throw std::invalid_argument("request fields");
    }
    if (operation == "request" && parsed->ByteSizeLong() > kRequestMaxBytes) {
      co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "command exceeds connector frame limit");
    }
    body = Json::parse(p::stream::json(*parsed));
    std::optional<std::uint64_t> expected;
    if (!read && operation != "request") {
      const std::string text(request[http::field::if_match]);
      if (text.size() < 3 || text.front() != '"' || text.back() != '"') {
        co_return p::fail(p::ErrorCode::kCommonPreconditionRequired, "If-Match required");
      }
      std::uint64_t version = 0;
      const auto result = std::from_chars(std::string_view(text).substr(1, text.size() - 2).begin(),
                                          std::string_view(text).substr(1, text.size() - 2).end(), version);
      if (result.ec != std::errc{} || result.ptr != std::string_view(text).substr(1, text.size() - 2).end()
          || version == 0) {
        throw std::invalid_argument("version");
      }
      expected = version;
    }
    auto result = co_await repository_->request(std::move(principal), operation, body,
                                                std::string(request["Idempotency-Key"]), expected);
    if (!result) {
      co_return std::unexpected(result.error());
    }
    Response response(http::status::ok, 11);
    response.set(http::field::content_type, "application/json");
    response.body() = result->dump();
    if (result->contains("command")) {
      response.set(http::field::etag, '"' + result->at("command").at("version").get<std::string>() + '"');
    }
    co_return response;
  } catch (const std::exception&) {
    co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid request fields");
  }
}
}  // namespace psim::command_service
