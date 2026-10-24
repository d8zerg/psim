#include "console_api.hpp"

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "console_store.hpp"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "report.hpp"

namespace psim::projection {
platform::Result<FeedFilter> feed_filter(std::string_view target, bool zones) {
  FeedFilter result;
  const auto question = target.find('?');
  if (question == std::string_view::npos) {
    return result;
  }
  auto rest = target.substr(question + 1);
  while (!rest.empty()) {
    const auto amp = rest.find('&');
    const auto pair = rest.substr(0, amp);
    const auto equal = pair.find('=');
    if (equal == std::string_view::npos) {
      return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid feed filter");
    }
    const auto name = pair.substr(0, equal);
    std::string value;
    const auto encoded = pair.substr(equal + 1);
    for (std::size_t i = 0; i < encoded.size(); ++i) {
      if (encoded[i] != '%') {
        value += encoded[i];
        continue;
      }
      unsigned int byte = 0;
      if (i + 2 >= encoded.size()) {
        return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid filter encoding");
      }
      const auto text = encoded.substr(i + 1, 2);
      const auto decoded = std::from_chars(text.begin(), text.end(), byte, 16);
      if (decoded.ec != std::errc{} || decoded.ptr != text.end()) {
        return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid filter encoding");
      }
      value += static_cast<char>(byte);
      i += 2;
    }
    if (!platform::stream::uuid(value)) {
      return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "filter identity must be a UUID");
    }
    if (name == "siteIds") {
      result.sites.insert(std::move(value));
    } else if (zones && name == "zoneIds") {
      result.zones.insert(std::move(value));
    } else {
      return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "unknown feed filter");
    }
    rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
  }
  return result;
}

boost::asio::awaitable<platform::Result<Response>> console_request(ConsoleStore* store, const Request* request,
                                                                   platform::security::Principal principal) {
  const auto target = request->target();
  const auto path = target.substr(0, target.find('?'));
  if (path == "/v1/feed/snapshot") {
    if (request->method() != http::verb::get || !request->body().empty()) {
      co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "bodyless GET required");
    }
    const auto allowed = platform::authz::decide_role(principal, "feed.read", std::chrono::system_clock::now());
    if (!allowed) {
      co_return std::unexpected(allowed.error());
    }
    const std::string query(target);
    const auto question = query.find('?');
    google::protobuf::Timestamp at;
    std::string location;
    std::string filters = "/v1/feed/incidents";
    if (question == std::string::npos) {
      co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "historical time required");
    }
    auto rest = std::string_view(query).substr(question + 1);
    while (!rest.empty()) {
      const auto amp = rest.find('&');
      const auto pair = rest.substr(0, amp);
      if (pair.starts_with("at=")) {
        std::string decoded;
        const auto encoded = pair.substr(3);
        for (std::size_t index = 0; index < encoded.size(); ++index) {
          if (encoded[index] != '%') {
            decoded += encoded[index];
            continue;
          }
          unsigned int byte = 0;
          if (index + 2 >= encoded.size()) {
            co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid time encoding");
          }
          const auto text = encoded.substr(index + 1, 2);
          const auto parsed = std::from_chars(text.begin(), text.end(), byte, 16);
          if (parsed.ec != std::errc{} || parsed.ptr != text.end() || byte == 0) {
            co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid time encoding");
          }
          decoded += static_cast<char>(byte);
          index += 2;
        }
        if (at.seconds() != 0 || !google::protobuf::util::TimeUtil::FromString(decoded, &at)) {
          co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid historical time");
        }
      } else if (pair.starts_with("locationId=")) {
        if (!location.empty() || !platform::stream::uuid(pair.substr(11))) {
          co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid historical location");
        }
        location = pair.substr(11);
      } else {
        filters += filters.find('?') == std::string::npos ? "?" : "&";
        filters += pair;
      }
      rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
    }
    const auto millis = google::protobuf::util::TimeUtil::TimestampToMilliseconds(at);
    if (at.seconds() <= 0 || at.seconds() > google::protobuf::util::TimeUtil::GetCurrentTime().seconds()) {
      co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "historical time out of range");
    }
    auto filter = feed_filter(filters, false);
    if (!filter) {
      co_return std::unexpected(filter.error());
    }
    auto snapshot = co_await store->snapshot(principal, location, std::move(filter->sites), millis);
    if (!snapshot) {
      co_return std::unexpected(snapshot.error());
    }
    *snapshot->history.mutable_feed() = snapshot->feed;
    *snapshot->history.mutable_location() = snapshot->location;
    Response result(http::status::ok, 11);
    result.set(http::field::content_type, "application/json");
    result.body() = platform::stream::json(snapshot->history);
    result.prepare_payload();
    co_return result;
  }
  const bool feed = path == "/v1/feed/incidents";
  const bool operators = path == "/v1/feed/operators";
  const bool location = path.starts_with("/v1/feed/locations/") && path.ends_with("/status");
  if (!feed && !operators && !location) {
    co_return platform::fail(platform::ErrorCode::kCommonNotFound, "unknown console operation");
  }
  if (request->method() != http::verb::get || !request->body().empty()) {
    co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "bodyless GET required");
  }
  if (!platform::stream::uuid(principal.tenant)) {
    co_return platform::fail(platform::ErrorCode::kAuthForbidden, "tenant identity required");
  }
  auto access = platform::authz::decide_role(principal, operators ? "feed.operators.read" : "feed.read",
                                             std::chrono::system_clock::now());
  if (!access) {
    co_return std::unexpected(access.error());
  }
  auto filter = feed_filter(target, feed);
  if (!filter) {
    co_return std::unexpected(filter.error());
  }
  const std::string identity = location ? std::string(path.substr(19, path.size() - 26)) : std::string{};
  if (location && (!platform::stream::uuid(identity) || target.find('?') != std::string_view::npos)) {
    co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid location identity or filter");
  }
  auto snapshot = co_await store->snapshot(principal, identity, std::move(filter->sites));
  if (!snapshot) {
    co_return std::unexpected(snapshot.error());
  }
  auto epoch = std::string((*request)["X-Psim-Realtime-Epoch"]);
  if (!epoch.empty() && !platform::stream::uuid(epoch)) {
    co_return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid realtime epoch");
  }
  nlohmann::json token{{"version", 2},
                       {"generation", snapshot->generation},
                       {"epoch", epoch},
                       {"tenant", principal.tenant},
                       {"issuedAtMs", std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count()}};
  std::map<std::string, std::int64_t> response_positions;
  for (const auto& [key, value] : snapshot->offsets) {
    const std::string prefix = "psim.response.events.v1:";
    if (key.starts_with(prefix)) {
      response_positions[key.substr(prefix.size())] = value;
    }
  }
  token["responseOffsets"] = response_positions;
  std::string body;
  if (feed) {
    std::map<std::string, std::int64_t> positions;
    const std::string prefix = "psim.incidents.events.v1:";
    for (const auto& [key, value] : snapshot->offsets) {
      if (key.starts_with(prefix)) {
        positions[key.substr(prefix.size())] = value;
      }
    }
    token["offsets"] = positions;
    auto* values = snapshot->feed.mutable_incidents();
    for (int i = values->size() - 1; i >= 0; --i) {
      if (!filter->zones.empty() && !filter->zones.contains(values->Get(i).zone_id())) {
        values->DeleteSubrange(i, 1);
      }
    }
    snapshot->feed.set_resume_token(token.dump());
    body = platform::stream::json(snapshot->feed);
  } else {
    token["stateOffsets"] = snapshot->offsets;
    token["locationId"] = identity;
    snapshot->location.set_resume_token(token.dump());
    snapshot->operators.set_resume_token(token.dump());
    body = location ? platform::stream::json(snapshot->location) : platform::stream::json(snapshot->operators);
  }
  Response result(http::status::ok, 11);
  result.set(http::field::content_type, "application/json");
  result.body() = std::move(body);
  co_return result;
}
}  // namespace psim::projection
