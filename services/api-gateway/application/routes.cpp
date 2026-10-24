#include "routes.hpp"

#include <google/api/annotations.pb.h>
#include <psim/api/v1/audit.pb.h>
#include <psim/api/v1/catalog.pb.h>
#include <psim/api/v1/commands.pb.h>
#include <psim/api/v1/events.pb.h>
#include <psim/api/v1/feed.pb.h>
#include <psim/api/v1/incidents.pb.h>
#include <psim/api/v1/me.pb.h>
#include <psim/api/v1/notifications.pb.h>
#include <psim/api/v1/operations.pb.h>
#include <psim/api/v1/reports.pb.h>
#include <psim/api/v1/response.pb.h>
#include <psim/api/v1/rules.pb.h>
#include <psim/api/v1/settings.pb.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::api_gateway {
namespace {
std::pair<std::string, std::string> destination(std::string_view service, std::string_view method, bool read) {
  if (service == "MeService") {
    return {"local", ""};
  }
  if (service == "CatalogService") {
    if (read) {
      return {"catalog", "catalog.read"};
    }
    return {"catalog", method.find("Connector") != std::string_view::npos ? "connector.write" : "catalog.write"};
  }
  if (service == "MappingService") {
    return {"catalog", read ? "mapping.read" : "mapping.write"};
  }
  if (service == "SettingsService") {
    return {"catalog", read ? "settings.read" : "settings.write"};
  }
  if (service == "CorrelationRuleService") {
    return {"rules", read ? "rule.read" : "rule.write"};
  }
  if (service == "EventHistoryService") {
    return {"history", method == "RuleStatistics" ? "rule.statistics.read" : "event.read"};
  }
  if (service == "FeedService") {
    return {"projection", method == "GetOperatorLoad" ? "feed.operators.read" : "feed.read"};
  }
  if (service == "CommandService") {
    if (read) {
      return {"commands", "command.read"};
    }
    if (method == "RequestCommand" || method == "PreviewCommand") {
      return {"commands", "command.request"};
    }
    return {"commands", method == "CancelCommand" ? "command.cancel" : "command.confirm"};
  }
  if (service == "ReportService") {
    return {"reports", method == "ExportIncidentReport" ? "report.export" : "report.read"};
  }
  if (service == "AuditService") {
    if (method == "VerifyAuditIntegrity") {
      return {"audit", "audit.verify"};
    }
    return {"audit", method == "ExportAuditRecords" ? "audit.export" : "audit.search"};
  }
  if (service == "NotificationService") {
    return {"notifications", "notification.read"};
  }
  if (service == "NotificationContactService") {
    return {"notifications",
            method == "SetNotificationContact" ? "notification.contact.write" : "notification.delivery.resolve"};
  }
  if (service == "ResponsePlanService") {
    return {"response", read ? "response.plan.read" : "response.plan.write"};
  }
  if (service == "ResponseRunService") {
    if (read) {
      return {"response", "response.run.read"};
    }
    return {"response", method == "OverrideStep" ? "response.step.override" : "response.step.execute"};
  }
  if (service == "EscalationPolicyService") {
    return {"response", read ? "response.policy.read" : "response.policy.write"};
  }
  if (service == "SiteDutyService") {
    return {"response", read ? "response.duty.read" : "response.duty.write"};
  }
  if (service == "OperationalService") {
    const std::map<std::string_view, std::string> actions = {{"UpdateAlarm", "update-alarm"},
                                                             {"SetPresence", "set-presence"},
                                                             {"AskAssistant", "ask-assistant"},
                                                             {"CreateExercise", "create-exercise"},
                                                             {"ActExercise", "act-exercise"}};
    return {"incidents", read ? "incident.read" : "incident." + actions.at(method)};
  }
  if (service == "IncidentService") {
    if (method.find("IncidentType") != std::string_view::npos) {
      return {"incidents", read ? "incident.types.read" : "incident.types.write"};
    }
    if (read) {
      return {"incidents", "incident.read"};
    }
    const std::map<std::string_view, std::string> actions = {{"AcknowledgeIncident", "acknowledge"},
                                                             {"StartIncidentWork", "start-work"},
                                                             {"AssignIncident", "assign"},
                                                             {"AddIncidentComment", "comment"},
                                                             {"AddIncidentLink", "link"},
                                                             {"ResolveIncident", "resolve"},
                                                             {"CloseIncident", "close"},
                                                             {"ReopenIncident", "reopen"},
                                                             {"SetIncidentPriority", "set-priority"},
                                                             {"TransferIncidents", "request-transfer"}};
    return {"incidents", "incident." + actions.at(method)};
  }
  throw std::logic_error("unregistered REST service");
}

bool matches(std::string_view pattern, std::string_view path) {
  while (!pattern.empty()) {
    if (pattern.front() != '{') {
      if (path.empty() || pattern.front() != path.front()) {
        return false;
      }
      pattern.remove_prefix(1);
      path.remove_prefix(1);
      continue;
    }
    const auto end = pattern.find('}');
    pattern.remove_prefix(end + 1);
    const auto length = path.find_first_of("/:");
    const auto value = path.substr(0, length);
    if (value.empty() || value == "." || value == "..") {
      return false;
    }
    path.remove_prefix(value.size());
  }
  return path.empty();
}
}  // namespace

const std::vector<Route>& routes() {
  static const auto kRoutes = [] {
    std::vector<Route> result;
    const std::array files = {api::v1::GetOperationsRequest::descriptor()->file(),
                              api::v1::GetSettingsRequest::descriptor()->file(),
                              api::v1::GetMeRequest::descriptor()->file(),
                              api::v1::GetIncidentFeedRequest::descriptor()->file(),
                              api::v1::GetIncidentRequest::descriptor()->file(),
                              api::v1::ListLocationsRequest::descriptor()->file(),
                              api::v1::ListRulesRequest::descriptor()->file(),
                              api::v1::GetEventRequest::descriptor()->file(),
                              api::v1::ListCommandsRequest::descriptor()->file(),
                              api::v1::ListPlansRequest::descriptor()->file(),
                              api::v1::ListMyNotificationsRequest::descriptor()->file(),
                              api::v1::SearchAuditRecordsRequest::descriptor()->file(),
                              api::v1::GetIncidentReportRequest::descriptor()->file()};
    for (const auto* file : files) {
      for (int s = 0; s < file->service_count(); ++s) {
        const auto* service = file->service(s);
        for (int m = 0; m < service->method_count(); ++m) {
          const auto* method = service->method(m);
          const auto& rule = method->options().GetExtension(google::api::http);
          const bool read = !rule.get().empty();
          auto verb = http::verb::put;
          std::string path = rule.put();
          if (read) {
            verb = http::verb::get;
            path = rule.get();
          } else if (!rule.post().empty()) {
            verb = http::verb::post;
            path = rule.post();
          }
          auto [owner, action] = destination(service->name(), method->name(), read);
          const bool precondition = verb == http::verb::put || (!read && path.find('{') != std::string::npos);
          result.push_back({verb, path, std::move(owner), std::move(action),
                            std::string(service->name()) + "_" + std::string(method->name()), verb == http::verb::post,
                            precondition, path.ends_with("/floor-plan")});
        }
      }
    }
    // Static suffixes must precede parameter matches, e.g. events/rule-statistics.
    std::ranges::stable_sort(result, [](const auto& a, const auto& b) { return a.path.find('{') > b.path.find('{'); });
    return result;
  }();
  return kRoutes;
}

platform::Result<const Route*> route(http::verb method, std::string_view target) {
  const auto path = target.substr(0, target.find('?'));
  if (target.size() > 8192 || path.find('%') != std::string_view::npos || path.find('\\') != std::string_view::npos
      || target.find('#') != std::string_view::npos) {
    return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "invalid request target");
  }
  for (const auto& item : routes()) {
    if (item.method == method && matches(item.path, path)) {
      return &item;
    }
  }
  return platform::fail(platform::ErrorCode::kCommonNotFound, "unknown REST operation");
}

std::uint64_t request_limit(const Route& item) {
  if (item.floor_plan) {
    return 20ULL * 1024 * 1024;
  }
  if (item.operation == "CatalogService_StartImport") {
    return 64ULL * 1024 * 1024;
  }
  return 1024ULL * 1024;
}

platform::Result<void> request_headers(const Route& item, const http::fields& fields) {
  const std::string key(fields["Idempotency-Key"]);
  if ((item.idempotency && key.empty()) || (!key.empty() && !platform::stream::uuid(key))) {
    return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "UUID Idempotency-Key required");
  }
  if (item.precondition) {
    const std::string value(fields[http::field::if_match]);
    if (value.empty()) {
      return platform::fail(platform::ErrorCode::kCommonPreconditionRequired, "If-Match required");
    }
    if (value.size() < 3 || value.front() != '"' || value.back() != '"') {
      return platform::fail(platform::ErrorCode::kCommonInvalidArgument, "quoted If-Match version required");
    }
  }
  return {};
}

void security_headers(http::fields& fields, std::string_view origin) {
  fields.set(http::field::cache_control, "no-store");
  fields.set(http::field::x_content_type_options, "nosniff");
  fields.set("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
  fields.set("Referrer-Policy", "no-referrer");
  fields.set("Strict-Transport-Security", "max-age=31536000");
  if (!origin.empty()) {
    fields.set(http::field::access_control_allow_origin, origin);
    fields.set(http::field::vary, "Origin");
    fields.set(http::field::access_control_expose_headers, "ETag, Content-Disposition, Retry-After");
  }
}

bool allowed_origin(std::string_view origin, const std::vector<std::string>& origins) {
  return origin.empty() || std::ranges::find(origins, origin) != origins.end();
}

bool RateLimiter::allow(std::string key, std::chrono::steady_clock::time_point now) {
  if (!buckets_.contains(key) && buckets_.size() >= capacity_) {
    std::erase_if(buckets_, [&](const auto& item) { return now - item.second.updated > std::chrono::minutes(5); });
    if (buckets_.size() >= capacity_) {
      return false;
    }
  }
  auto [it, inserted] = buckets_.try_emplace(std::move(key), Bucket{static_cast<double>(burst_), now});
  (void)inserted;
  auto& bucket = it->second;
  bucket.tokens = std::min(
      static_cast<double>(burst_),
      bucket.tokens + (std::chrono::duration<double>(now - bucket.updated).count() * static_cast<double>(rate_)));
  bucket.updated = now;
  if (bucket.tokens < 1) {
    return false;
  }
  bucket.tokens -= 1;
  return true;
}
}  // namespace psim::api_gateway
