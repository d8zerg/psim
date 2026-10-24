#include "http.hpp"

#include <google/protobuf/util/json_util.h>
#include <psim/api/v1/incidents.pb.h>
#include <psim/api/v1/operations.pb.h>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "repository.hpp"

namespace psim::incident_service {
namespace {
std::string unquote(std::string_view input) {
  std::string output;
  for (std::size_t index = 0; index < input.size(); ++index) {
    if (input[index] == '%') {
      unsigned int value = 0;
      if (index + 2 >= input.size()) {
        throw std::invalid_argument("query encoding");
      }
      const auto parsed =
          std::from_chars(input.substr(index + 1, 2).begin(), input.substr(index + 1, 2).end(), value, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != input.substr(index + 1, 2).end() || value == 0) {
        throw std::invalid_argument("query encoding");
      }
      output += static_cast<char>(value);
      index += 2;
    } else {
      output += input[index] == '+' ? ' ' : input[index];
    }
  }
  return output;
}

Response success(Json body, http::status status = http::status::ok) {
  Response response(status, 11);
  response.set(http::field::content_type, "application/json");
  if (body.contains("incident")) {
    response.set(http::field::etag, '"' + body["incident"].at("version").get<std::string>() + '"');
  }
  if (body.contains("incidentType")) {
    response.set(http::field::etag, '"' + body["incidentType"].at("version").get<std::string>() + '"');
  }
  response.body() = body.dump();
  return response;
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
  const auto started = std::chrono::steady_clock::now();
  try {
    const std::string target(request.target());
    const auto question = target.find('?');
    const auto path = target.substr(0, question);
    std::string operation;
    std::string id;
    Json body = Json::object();
    const bool read = request.method() == http::verb::get;
    if (read) {
      auto rest = question == std::string::npos ? std::string_view{} : std::string_view(target).substr(question + 1);
      while (!rest.empty()) {
        const auto amp = rest.find('&');
        const auto pair = rest.substr(0, amp);
        const auto equal = pair.find('=');
        if (equal == std::string_view::npos) {
          throw std::invalid_argument("query");
        }
        auto key = unquote(pair.substr(0, equal));
        auto value = unquote(pair.substr(equal + 1));
        if (key == "pageSize") {
          body[key] = std::stoi(value);
        } else if (key == "escalatedOnly") {
          if (value != "true" && value != "false") {
            throw std::invalid_argument("boolean");
          }
          body[key] = value == "true";
        } else if (key == "states" || key == "priorities" || key == "siteIds" || key == "zoneIds"
                   || key == "incidentTypeIds") {
          if (!body.contains(key)) {
            body[key] = Json::array();
          }
          body[key].push_back(value);
        } else if (!body.contains(key)) {
          body[key] = value;
        } else {
          throw std::invalid_argument("duplicate query");
        }
        rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
      }
    } else {
      if (request.method() != http::verb::post && request.method() != http::verb::put) {
        co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown method");
      }
      if (request[http::field::content_type] != "application/json") {
        co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "JSON content type required");
      }
      body = request.body().empty() ? Json::object() : Json::parse(request.body());
      if (!body.is_object()) {
        throw std::invalid_argument("body");
      }
    }
    const std::string key(request["Idempotency-Key"]);
    std::optional<std::uint64_t> expected;
    if (!read) {
      auto text = std::string(request[http::field::if_match]);
      if (!text.empty()) {
        if (text.size() < 3 || text.front() != '"' || text.back() != '"') {
          throw std::invalid_argument("If-Match");
        }
        text = text.substr(1, text.size() - 2);
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(std::string_view(text).begin(), std::string_view(text).end(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != std::string_view(text).end() || value == 0
            || value >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
          throw std::invalid_argument("If-Match");
        }
        expected = value;
      }
    }
    if (path == "/v1/incidents:transfer" && request.method() == http::verb::post) {
      api::v1::TransferIncidentsRequest batch;
      if (!google::protobuf::util::JsonStringToMessage(body.dump(), &batch).ok() || batch.items_size() < 1
          || batch.items_size() > 100 || !p::stream::uuid(key)) {
        throw std::invalid_argument("transfer batch");
      }
      Json output = {{"results", Json::array()}};
      std::size_t index = 0;
      for (const auto& item : batch.items()) {
        const Json command = {
            {"recipientId", item.recipient_id()}, {"comment", item.comment()}, {"transferId", item.transfer_id()}};
        const auto item_key = p::stream::uuid5("ea5e4ec6-3cb5-4d50-92e6-16f430292702", {key, std::to_string(index++)});
        auto result = co_await repository_->request(
            principal, item.transfer_id().empty() ? "request-transfer" : "confirm-transfer", item.incident_id(),
            command, item_key, item.expected_version());
        if (result) {
          output["results"].push_back({{"incidentId", item.incident_id()}, {"incident", (*result)["incident"]}});
        } else {
          output["results"].push_back({{"incidentId", item.incident_id()}, {"errorCode", result.error().name()}});
        }
      }
      co_return success(output);
    }
    if (path == "/v1/incident-types") {
      operation = read ? "types.list" : "types.create";
    } else if (path.starts_with("/v1/incident-types/") && request.method() == http::verb::put) {
      operation = "types.update";
      id = path.substr(19);
    } else if (path == "/v1/incidents" && read) {
      operation = "list";
    } else if (path.starts_with("/v1/incidents/")) {
      auto suffix = path.substr(14);
      const auto split = suffix.find_first_of("/:");
      id = suffix.substr(0, split);
      operation = split == std::string::npos ? "get" : suffix.substr(split + 1);
      if (operation == "comments") {
        operation = "comment";
      }
      if (operation == "links") {
        operation = "link";
      }
      if (operation.starts_with("exercises/")) {
        auto exercise = operation.substr(10);
        const auto action = exercise.find(':');
        if ((read && action != std::string::npos) || body.contains("exerciseId") || body.contains("exercise_id")) {
          throw std::invalid_argument("exercise path identity");
        }
        body["exerciseId"] = exercise.substr(0, action);
        if (!read && (action == std::string::npos || exercise.substr(action + 1) != "act")) {
          throw std::invalid_argument("exercise action");
        }
        operation = read ? "get-exercise" : "act-exercise";
      }
      const bool operational_read = operation == "operations" || operation == "snapshots" || operation == "recovery"
                                    || operation == "get-exercise";
      if ((read && !operational_read && operation != "get" && operation != "timeline" && operation != "signals")
          || (!read && (operational_read || operation == "get" || operation == "timeline" || operation == "signals"))) {
        co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown endpoint");
      }
    } else {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown endpoint");
    }
    // Decode against the published request schema before touching storage.
    const std::map<std::string, std::string> messages = {{"operations", "GetOperationsRequest"},
                                                         {"update-alarm", "UpdateAlarmRequest"},
                                                         {"set-presence", "SetPresenceRequest"},
                                                         {"snapshots", "GetIncidentSnapshotsRequest"},
                                                         {"recovery", "GetIncidentRecoveryRequest"},
                                                         {"ask-assistant", "AskAssistantRequest"},
                                                         {"create-exercise", "CreateExerciseRequest"},
                                                         {"get-exercise", "GetExerciseRequest"},
                                                         {"act-exercise", "ActExerciseRequest"},
                                                         {"list", "ListIncidentsRequest"},
                                                         {"get", "GetIncidentRequest"},
                                                         {"timeline", "ListTimelineRequest"},
                                                         {"signals", "ListIncidentSignalsRequest"},
                                                         {"acknowledge", "AcknowledgeIncidentRequest"},
                                                         {"start-work", "StartIncidentWorkRequest"},
                                                         {"assign", "AssignIncidentRequest"},
                                                         {"set-priority", "SetIncidentPriorityRequest"},
                                                         {"comment", "AddIncidentCommentRequest"},
                                                         {"link", "AddIncidentLinkRequest"},
                                                         {"resolve", "ResolveIncidentRequest"},
                                                         {"reopen", "ReopenIncidentRequest"},
                                                         {"close", "CloseIncidentRequest"},
                                                         {"types.list", "ListIncidentTypesRequest"}};
    if (messages.contains(operation)) {
      const auto* descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "psim.api.v1." + messages.at(operation));
      const std::unique_ptr<google::protobuf::Message> parsed(
          google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor)->New());
      auto input = body;
      if (!id.empty()) {
        if (input.contains("incidentId") && input["incidentId"] != id) {
          throw std::invalid_argument("identity");
        }
        input["incidentId"] = id;
      }
      if (!google::protobuf::util::JsonStringToMessage(input.dump(), parsed.get()).ok()) {
        throw std::invalid_argument("request fields");
      }
      std::string normalized;
      if (!google::protobuf::util::MessageToJsonString(*parsed, &normalized).ok()) {
        throw std::invalid_argument("request encoding");
      }
      body = Json::parse(normalized);
      body.erase("incidentId");
      if (read
          && (operation == "list" || operation == "timeline" || operation == "signals" || operation == "types.list")) {
        if (!body.contains("pageSize")) {
          body["pageSize"] = 100;
        }
      }
    } else if (!operation.starts_with("types.")) {
      co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown action");
    }
    auto result = co_await repository_->request(principal, operation, id, body, key, expected);
    std::string outcome = "success";
    if (!result) {
      outcome = "failed";
      if (result.error().code() == p::ErrorCode::kAuthForbidden
          || result.error().code() == p::ErrorCode::kAuthScopeDenied) {
        outcome = "denied";
      }
    } else if (result->value("unavailable", false)) {
      outcome = "unavailable";
    }
    if (operations_) {
      operations_->Add({{"operation", operation}, {"outcome", outcome}}).Increment();
    }
    if (latency_) {
      latency_
          ->Add({{"operation", operation}}, prometheus::Histogram::BucketBoundaries{0.005, 0.02, 0.1, 0.2, 1, 5, 10})
          .Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    }
    if (!result) {
      co_return std::unexpected(result.error());
    }
    co_return success(*result, operation == "types.create" ? http::status::created : http::status::ok);
  } catch (const std::exception&) {
    co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid request fields");
  }
}
}  // namespace psim::incident_service
