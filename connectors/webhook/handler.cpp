#include "handler.hpp"

#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <psim/connector/v1/connector.pb.h>

#include <algorithm>
#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/connector/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::connectors::webhook {
namespace {
Response response(http::status status, const std::string& body) {
  Response result{status, 11};
  result.set(http::field::content_type, "application/json");
  result.set(http::field::cache_control, "no-store");
  result.keep_alive(false);
  result.body() = body;
  result.prepare_payload();
  return result;
}

bool safe_key(const std::string& key) {
  return !key.empty() && key.size() <= 128 && std::ranges::all_of(key, [](unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9')
           || value == '-' || value == '_' || value == '.' || value == ':';
  });
}

::psim::connector::v1::ConnectorEvent parse(const std::string& body) {
  if (body.size() > 65536) {
    throw std::invalid_argument("body too large");
  }
  bool duplicate = false;
  std::vector<std::set<std::string>> keys;
  const auto json = nlohmann::json::parse(body, [&](int /*depth*/, auto event, auto& parsed) {
    if (event == nlohmann::json::parse_event_t::object_start) {
      keys.emplace_back();
    } else if (event == nlohmann::json::parse_event_t::key) {
      duplicate = !keys.back().insert(parsed.template get<std::string>()).second || duplicate;
    } else if (event == nlohmann::json::parse_event_t::object_end) {
      keys.pop_back();
    }
    return true;
  });
  if (!json.is_object() || duplicate || json.size() < 3 || json.size() > 4 || !json.contains("source_ref")
      || !json.contains("raw_code") || !json.contains("occurred_at")) {
    throw std::invalid_argument("invalid event fields");
  }
  for (const auto& [key, value] : json.items()) {
    if (key != "source_ref" && key != "raw_code" && key != "occurred_at" && key != "attributes") {
      throw std::invalid_argument("unknown event field");
    }
  }
  ::psim::connector::v1::ConnectorEvent event;
  event.set_source_ref(json.at("source_ref").get<std::string>());
  event.set_raw_code(json.at("raw_code").get<std::string>());
  if (event.source_ref().empty() || event.source_ref().size() > 256 || event.raw_code().empty()
      || event.raw_code().size() > 128
      || !google::protobuf::util::TimeUtil::FromString(json.at("occurred_at").get<std::string>(),
                                                       event.mutable_occurred_at())) {
    throw std::invalid_argument("invalid event value");
  }
  if (json.contains("attributes")) {
    const auto& attributes = json.at("attributes");
    if (!attributes.is_object() || attributes.size() > 64) {
      throw std::invalid_argument("invalid attributes");
    }
    for (const auto& [key, value] : attributes.items()) {
      if (key.empty() || key.size() > 256 || !value.is_string() || value.get_ref<const std::string&>().size() > 4096) {
        throw std::invalid_argument("invalid attribute");
      }
      (*event.mutable_attributes())[key] = value.get<std::string>();
    }
  }
  if (event.ByteSizeLong() > 64000) {
    throw std::invalid_argument("event too large");
  }
  return event;
}
}  // namespace

Handler::Handler(platform::connector::Client& client, std::string token) : client_(&client), token_(std::move(token)) {
  if (token_.size() < 32 || token_.size() > 256 || token_.find_first_of("\r\n\t ") != std::string::npos) {
    throw std::invalid_argument("webhook token must contain 32 to 256 non-whitespace bytes");
  }
}

Response Handler::handle(const Request& request) const {
  const auto authorization = request[http::field::authorization];
  const auto expected = "Bearer " + token_;
  if (request.count(http::field::authorization) != 1 || authorization.size() != expected.size()
      || CRYPTO_memcmp(authorization.data(), expected.data(), expected.size()) != 0) {
    auto result = response(http::status::unauthorized, R"({"code":"unauthorized"})");
    result.set(http::field::www_authenticate, "Bearer");
    return result;
  }
  if (request.method() == http::verb::get && request.target() == "/healthz") {
    const auto data = client_->diagnostics();
    return response(http::status::ok,
                    nlohmann::json{{"connected", data.connected}, {"pending", data.pending_events}}.dump());
  }
  if (request.method() == http::verb::get && request.target() == "/metrics") {
    auto result = response(http::status::ok, client_->metrics());
    result.set(http::field::content_type, "text/plain; version=0.0.4");
    return result;
  }
  if (request.target() != "/v1/events") {
    return response(http::status::not_found, R"({"code":"not_found"})");
  }
  if (request.method() != http::verb::post) {
    auto result = response(http::status::method_not_allowed, R"({"code":"method_not_allowed"})");
    result.set(http::field::allow, "POST");
    return result;
  }
  if (request.count(http::field::content_type) != 1 || request[http::field::content_type] != "application/json") {
    return response(http::status::unsupported_media_type, R"({"code":"content_type"})");
  }
  const std::string key(request["Idempotency-Key"]);
  if (request.count("Idempotency-Key") != 1 || !safe_key(key)) {
    return response(http::status::bad_request, R"({"code":"idempotency_key"})");
  }
  ::psim::connector::v1::ConnectorEvent event;
  try {
    event = parse(request.body());
  } catch (const std::exception&) {
    return response(http::status::bad_request, R"({"code":"invalid_event"})");
  }
  const auto stored = client_->publish_once(std::move(event), key);
  if (!stored) {
    if (stored.error().code() == platform::ErrorCode::kCommonIdempotencyKeyReused) {
      return response(http::status::conflict, R"({"code":"idempotency_conflict"})");
    }
    auto result = response(http::status::service_unavailable, R"({"code":"buffer_unavailable"})");
    result.set(http::field::retry_after, "1");
    return result;
  }
  return response(http::status::accepted, nlohmann::json{{"event_id", stored->event_id()},
                                                         {"source_epoch", stored->source_epoch()},
                                                         {"source_seq", stored->source_seq()},
                                                         {"status", "buffered"}}
                                              .dump());
}
}  // namespace psim::connectors::webhook
