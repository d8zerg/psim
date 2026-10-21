#include "http.hpp"

#include <google/protobuf/util/json_util.h>

#include <charconv>
#include <cstddef>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "psim/api/v1/events.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::event_history {
namespace {
std::string unquote(std::string_view input) {
  std::string output;
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%') {
      unsigned int value = 0;
      if (i + 2 >= input.size()) {
        throw std::invalid_argument("encoding");
      }
      auto parsed = std::from_chars(input.substr(i + 1, 2).begin(), input.substr(i + 1, 2).end(), value, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != input.substr(i + 1, 2).end() || value == 0) {
        throw std::invalid_argument("encoding");
      }
      output += static_cast<char>(value);
      i += 2;
    } else {
      output += input[i] == '+' ? ' ' : input[i];
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

boost::asio::awaitable<p::Result<Response>> Api::handle(Request request, p::security::Principal principal) {
  try {
    if (request.method() != http::verb::get) {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown history method");
    }
    const std::string target(request.target());
    const auto question = target.find('?');
    const auto path = target.substr(0, question);
    std::string operation;
    std::string message;
    std::string id;
    if (path == "/v1/events") {
      operation = "search";
      message = "SearchEventsRequest";
    } else if (path == "/v1/events/rule-statistics") {
      operation = "statistics";
      message = "RuleStatisticsRequest";
    } else if (path.starts_with("/v1/events/")) {
      operation = "get";
      message = "GetEventRequest";
      id = path.substr(11);
    } else if (path.starts_with("/v1/incidents/") && path.ends_with("/events")) {
      operation = "incident";
      message = "ListIncidentEventsRequest";
      id = path.substr(14, path.size() - 21);
    } else {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown history endpoint");
    }
    Json input = Json::object();
    auto rest = question == std::string::npos ? std::string_view{} : std::string_view(target).substr(question + 1);
    while (!rest.empty()) {
      const auto amp = rest.find('&');
      const auto equal = rest.substr(0, amp).find('=');
      if (equal == std::string_view::npos) {
        throw std::invalid_argument("query");
      }
      const auto key = unquote(rest.substr(0, equal));
      const auto value = unquote(rest.substr(equal + 1, amp == std::string_view::npos ? amp : amp - equal - 1));
      if (key == "siteIds" || key == "zoneIds" || key == "deviceIds" || key == "types" || key == "ruleIds") {
        if (!input.contains(key)) {
          input[key] = Json::array();
        }
        input[key].push_back(value);
      } else {
        if (input.contains(key)) {
          throw std::invalid_argument("duplicate query field");
        }
        if (key == "pageSize") {
          int size = 0;
          const auto parsed = std::from_chars(std::string_view(value).begin(), std::string_view(value).end(), size);
          if (parsed.ec != std::errc{} || parsed.ptr != std::string_view(value).end()) {
            throw std::invalid_argument("page size");
          }
          input[key] = size;
        } else if (key == "significantOnly") {
          if (value != "true" && value != "false") {
            throw std::invalid_argument("boolean");
          }
          input[key] = value == "true";
        } else {
          input[key] = value;
        }
      }
      rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
    }
    const std::string field = operation == "incident" ? "incidentId" : "eventId";
    if (!id.empty()) {
      if (input.contains(field)) {
        throw std::invalid_argument("path identity");
      }
      input[field] = id;
    }
    const auto* descriptor =
        google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName("psim.api.v1." + message);
    std::unique_ptr<google::protobuf::Message> const parsed(
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor)->New());
    if (!google::protobuf::util::JsonStringToMessage(input.dump(), parsed.get()).ok()) {
      throw std::invalid_argument("fields");
    }
    std::string normalized;
    if (!google::protobuf::util::MessageToJsonString(*parsed, &normalized).ok()) {
      throw std::invalid_argument("encoding");
    }
    input = Json::parse(normalized);
    input.erase(field);
    auto result = co_await search_->read(std::move(principal), operation, id, input);
    if (!result) {
      co_return std::unexpected(result.error());
    }
    Response response(http::status::ok, 11);
    response.set(http::field::content_type, "application/json");
    response.body() = result->dump();
    co_return response;
  } catch (const std::exception&) {
    co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid history request");
  }
}
}  // namespace psim::event_history
