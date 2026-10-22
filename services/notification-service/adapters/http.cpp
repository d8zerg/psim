#include "http.hpp"

#include <google/protobuf/util/json_util.h>
#include <psim/api/v1/notifications.pb.h>

#include <charconv>
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

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::notification_service {
Response Api::problem(const p::Error& error) {
  const auto& info = p::info(error.code());
  Response result(static_cast<http::status>(info.http_status), 11);
  result.set(http::field::content_type, "application/problem+json");
  result.body() =
      Json{{"type", "about:blank"}, {"title", info.title}, {"status", info.http_status}, {"code", error.name()}}.dump();
  return result;
}

asio::awaitable<p::Result<Response>> Api::handle(Request request, p::security::Principal principal) {
  try {
    const std::string target(request.target());
    const auto question = target.find('?');
    const auto path = target.substr(0, question);
    const bool read = request.method() == http::verb::get;
    Json body = Json::object();
    std::string operation;
    std::string type;
    if (read) {
      auto rest = question == std::string::npos ? std::string_view{} : std::string_view(target).substr(question + 1);
      while (!rest.empty()) {
        const auto amp = rest.find('&');
        const auto equal = rest.find('=');
        const auto pair = rest.substr(0, amp);
        if (equal == std::string_view::npos || equal >= pair.size()) {
          throw std::invalid_argument("query");
        }
        const auto name = std::string(pair.substr(0, equal));
        const auto value = std::string(pair.substr(equal + 1));
        if (body.contains(name)) {
          throw std::invalid_argument("query");
        }
        if (name == "unreadOnly") {
          if (value != "true" && value != "false") {
            throw std::invalid_argument("boolean");
          }
          body[name] = value == "true";
        } else if (name == "pageSize") {
          int size = 0;
          const auto parsed = std::from_chars(std::string_view(value).begin(), std::string_view(value).end(), size);
          if (parsed.ec != std::errc{} || parsed.ptr != std::string_view(value).end()) {
            throw std::invalid_argument("page");
          }
          body[name] = size;
        } else {
          body[name] = value;
        }
        rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
      }
    } else {
      if (question != std::string::npos || request[http::field::content_type] != "application/json") {
        throw std::invalid_argument("JSON required");
      }
      body = Json::parse(request.body().empty() ? "{}" : request.body());
      if (!body.is_object()) {
        throw std::invalid_argument("object");
      }
    }
    const auto identity = [&body](const std::string& name, std::string id) {
      if (body.contains(name) && body.at(name) != id) {
        throw std::invalid_argument("path identity");
      }
      body[name] = std::move(id);
    };
    if (path == "/v1/me/notifications" && read) {
      operation = "list";
      type = "psim.api.v1.ListMyNotificationsRequest";
    } else if (path.starts_with("/v1/me/notifications/") && path.ends_with(":read")
               && request.method() == http::verb::post) {
      operation = "read";
      type = "psim.api.v1.MarkNotificationReadRequest";
      identity("notificationId", path.substr(21, path.size() - 26));
    } else if (path.starts_with("/v1/notification-contacts/") && request.method() == http::verb::put) {
      operation = "contact";
      type = "psim.api.v1.NotificationContact";
      identity("userId", path.substr(26));
    } else if (path.starts_with("/v1/notifications/") && path.ends_with(":resolve")
               && request.method() == http::verb::post) {
      operation = "resolve";
      type = "psim.api.v1.ResolveUnknownDeliveryRequest";
      identity("notificationId", path.substr(18, path.size() - 26));
    } else {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown endpoint");
    }
    (void)api::v1::ListMyNotificationsRequest::descriptor();
    const auto* descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type);
    const std::unique_ptr<google::protobuf::Message> parsed(
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor)->New());
    if (!google::protobuf::util::JsonStringToMessage(body.dump(), parsed.get()).ok()) {
      throw std::invalid_argument("request fields");
    }
    body = Json::parse(p::stream::json(*parsed));
    std::optional<std::uint64_t> expected;
    if (!request[http::field::if_match].empty()) {
      const std::string text(request[http::field::if_match]);
      if (text.size() < 3 || text.front() != '"' || text.back() != '"') {
        throw std::invalid_argument("If-Match");
      }
      const auto value = std::string_view(text).substr(1, text.size() - 2);
      std::uint64_t version = 0;
      const auto result = std::from_chars(std::string_view(value).begin(), std::string_view(value).end(), version);
      if (result.ec != std::errc{} || result.ptr != std::string_view(value).end()) {
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
    for (const auto* field : {"notification", "contact"}) {
      if (result->contains(field)) {
        response.set(http::field::etag, '"' + result->at(field).at("version").get<std::string>() + '"');
      }
    }
    co_return response;
  } catch (const std::exception&) {
    co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid request fields");
  }
}
}  // namespace psim::notification_service
