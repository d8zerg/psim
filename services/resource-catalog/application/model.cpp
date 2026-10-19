#include "model.hpp"

#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/catalog/v1/maintenance.pb.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::resource_catalog {
namespace {
using Code = p::ErrorCode;
using Util = google::protobuf::util::TimeUtil;

p::Result<std::vector<std::vector<std::string>>> csv(std::string_view bytes) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string cell;
  bool quoted = false;
  bool closed = false;
  bool start = true;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const auto ch = bytes[i];
    if (quoted) {
      if (ch == '"') {
        if (i + 1 < bytes.size() && bytes[i + 1] == '"') {
          cell += '"';
          ++i;
        } else {
          quoted = false;
          closed = true;
        }
      } else {
        cell += ch;
      }
      continue;
    }
    if (ch == '"' && start) {
      quoted = true;
      start = false;
    } else if (ch == ',' || ch == '\n' || ch == '\r') {
      row.push_back(std::move(cell));
      cell.clear();
      start = true;
      closed = false;
      if (ch != ',') {
        if (ch == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') {
          ++i;
        }
        rows.push_back(std::move(row));
        row.clear();
        if (rows.size() > kMaxImportRows + 1) {
          return p::fail(Code::kCommonInvalidArgument, "import row limit exceeded");
        }
      }
    } else if (closed || ch == '"') {
      return p::fail(Code::kCommonInvalidArgument, "invalid CSV quoting");
    } else {
      start = false;
      cell += ch;
    }
  }
  if (quoted) {
    return p::fail(Code::kCommonInvalidArgument, "unterminated CSV field");
  }
  if (!cell.empty() || !row.empty() || closed) {
    row.push_back(std::move(cell));
    rows.push_back(std::move(row));
  }
  return rows;
}
}  // namespace

Json read_json(std::string_view bytes) {
  std::map<int, std::set<std::string>> keys;
  return Json::parse(bytes, [&keys](int depth, Json::parse_event_t event, Json& value) {
    if (depth > 32) {
      throw std::invalid_argument("JSON nesting limit exceeded");
    }
    if (event == Json::parse_event_t::object_start) {
      keys[depth + 1].clear();
    }
    if (event == Json::parse_event_t::key && !keys[depth].insert(value.get<std::string>()).second) {
      throw std::invalid_argument("duplicate JSON field");
    }
    return true;
  });
}

bool canonical_uuid(std::string_view value) {
  return p::stream::uuid(value) && std::ranges::none_of(value, [](char ch) { return ch >= 'A' && ch <= 'F'; });
}

std::string timestamp(Time now) {
  const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
  return Util::ToString(Util::NanosecondsToTimestamp(nanos));
}

p::Result<Time> time(std::string_view value) {
  google::protobuf::Timestamp parsed;
  if (!Util::FromString(std::string(value), &parsed) || parsed.seconds() < 0 || parsed.seconds() > 4102444800LL) {
    return p::fail(Code::kCommonInvalidArgument, "invalid UTC timestamp");
  }
  return Time(std::chrono::nanoseconds(Util::TimestampToNanoseconds(parsed)));
}

bool allowed(const p::security::Principal& principal, std::string_view site, bool write) {
  if (!canonical_uuid(principal.tenant) || principal.subject.empty()
      || principal.expires_at <= std::chrono::system_clock::now()) {
    return false;
  }
  const bool role =
      write ? principal.roles.contains("admin") : std::ranges::any_of(principal.roles, [](const auto& value) {
        return value == "admin" || value == "integrator" || value == "operator" || value == "supervisor"
               || value == "auditor" || value == "manager";
      });
  return role && (principal.scopes.contains("*") || (!site.empty() && principal.scopes.contains(std::string(site))));
}

p::Result<void> validate_type(const Json& body) {
  std::set<std::string> kinds;
  std::set<std::string> commands;
  for (const auto& source : body.value("sourceTemplates", Json::array())) {
    if (source.value("kind", "").empty() || !kinds.insert(source.at("kind").get<std::string>()).second) {
      return p::fail(Code::kCommonInvalidArgument, "source template kinds must be nonempty and unique");
    }
  }
  for (const auto& capability : body.value("capabilities", Json::array())) {
    const auto code = capability.value("code", "");
    if (code.empty() || code.size() > 128 || code.front() < 'a' || code.front() > 'z'
        || !std::ranges::all_of(
            code,
            [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.'; })
        || !commands.insert(code).second) {
      return p::fail(Code::kCommonInvalidArgument, "invalid or duplicate capability code");
    }
    std::set<std::string> parameters;
    for (const auto& parameter : capability.value("parameters", Json::array())) {
      if (parameter.value("name", "").empty()
          || parameter.value("type", "PARAMETER_TYPE_UNSPECIFIED") == "PARAMETER_TYPE_UNSPECIFIED"
          || !parameters.insert(parameter.at("name").get<std::string>()).second) {
        return p::fail(Code::kCommonInvalidArgument, "invalid capability parameter");
      }
    }
  }
  return {};
}

p::Result<void> validate_maintenance(const Json& body, Time now) {
  if (!body.contains("startsAt") || !body.contains("endsAt")) {
    return p::fail(Code::kCommonInvalidArgument, "maintenance requires start and end");
  }
  const auto start = time(body.at("startsAt").get<std::string>());
  const auto end = time(body.at("endsAt").get<std::string>());
  const auto reason = body.value("reason", "");
  const auto responsible = body.value("responsible", "");
  const auto policy = body.value("suppression", Json::object());
  if (!start || !end || *end <= *start || *end <= now || reason.empty() || reason.size() > 2000 || responsible.empty()
      || responsible.size() > 256
      || (policy.value("eventTypes", Json::array()).empty() && policy.value("ruleIds", Json::array()).empty())) {
    return p::fail(Code::kCommonInvalidArgument, "invalid maintenance interval or suppression policy");
  }
  std::set<std::string> seen;
  for (const auto& type : policy.value("eventTypes", Json::array())) {
    const auto value = type.get<std::string>();
    if (value.empty() || value.size() > 200 || value.front() < 'a' || value.front() > 'z'
        || !std::ranges::all_of(
            value,
            [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_'; })
        || value.starts_with("fire.") || value == "fire" || !seen.insert(value).second || seen.size() > 100) {
      return p::fail(Code::kCommonInvalidArgument, "fire, empty or duplicate event types cannot be suppressed");
    }
  }
  seen.clear();
  for (const auto& rule : policy.value("ruleIds", Json::array())) {
    const auto value = rule.get<std::string>();
    if (!canonical_uuid(value) || !seen.insert(value).second || seen.size() > 100) {
      return p::fail(Code::kCommonInvalidArgument, "invalid or duplicate suppression rule");
    }
  }
  return {};
}

p::Result<std::string> maintenance_transition(Json& body, Time now) {
  const auto start = time(body.at("startsAt").get<std::string>());
  const auto end = time(body.at("endsAt").get<std::string>());
  if (!start || !end) {
    return p::fail(Code::kCommonInternal, "stored maintenance interval is invalid");
  }
  const auto state = body.value("state", "MAINTENANCE_STATE_SCHEDULED");
  if (state == "MAINTENANCE_STATE_CANCELLED" || state == "MAINTENANCE_STATE_COMPLETED") {
    return std::string{};
  }
  if (now >= *end) {
    body["state"] = "MAINTENANCE_STATE_COMPLETED";
    body["completedAt"] = timestamp(*end);
    return std::string("complete");
  }
  if (now >= *start && state != "MAINTENANCE_STATE_ACTIVE") {
    body["state"] = "MAINTENANCE_STATE_ACTIVE";
    body["activatedAt"] = timestamp(now);
    return std::string("activate");
  }
  return std::string{};
}

p::Result<Resource> parse(std::string kind, const Json& input) {
  try {
    std::unique_ptr<google::protobuf::Message> message;
    std::string field;
    if (kind == "location") {
      message = std::make_unique<catalog::v1::Location>();
      field = "locationId";
    } else if (kind == "device_type") {
      message = std::make_unique<catalog::v1::DeviceType>();
      field = "deviceTypeId";
    } else if (kind == "device") {
      message = std::make_unique<catalog::v1::Device>();
      field = "deviceId";
    } else if (kind == "maintenance") {
      message = std::make_unique<catalog::v1::MaintenanceWindow>();
      field = "maintenanceId";
    } else if (kind == "connector") {
      message = std::make_unique<catalog::v1::Connector>();
      field = "connectorId";
    } else {
      return p::fail(Code::kCommonInvalidArgument, "unsupported resource type");
    }
    if (!input.is_object() || input.dump().size() > 65536
        || !google::protobuf::util::JsonStringToMessage(input.dump(), message.get()).ok()) {
      return p::fail(Code::kCommonInvalidArgument, "invalid resource JSON");
    }
    std::string encoded;
    if (!google::protobuf::util::MessageToJsonString(*message, &encoded).ok()) {
      return p::fail(Code::kCommonInvalidArgument, "invalid resource encoding");
    }
    auto body = Json::parse(encoded);
    auto id = body.value(field, "");
    if (id.empty()) {
      id = p::stream::uuid7();
      body[field] = id;
    }
    if (!canonical_uuid(id)) {
      return p::fail(Code::kCommonInvalidArgument, "resource id must be a lowercase UUID");
    }
    for (const auto* key : {"siteId", "parentId", "zoneId", "deviceTypeId", "connectorId"}) {
      if (body.contains(key) && !body.at(key).get<std::string>().empty()
          && !canonical_uuid(body.at(key).get<std::string>())) {
        return p::fail(Code::kCommonInvalidArgument, "reference must be a lowercase UUID");
      }
    }
    const auto state = body.value("state", "RESOURCE_STATE_ACTIVE");
    if (kind != "maintenance" && state != "RESOURCE_STATE_ACTIVE" && state != "RESOURCE_STATE_DISABLED"
        && state != "RESOURCE_STATE_ARCHIVED") {
      return p::fail(Code::kCommonInvalidArgument, "invalid resource state");
    }
    if (kind != "maintenance") {
      body["state"] = state;
    }
    if ((kind == "location" || kind == "device_type")
        && (body.value("code", "").empty() || body.value("code", "").size() > 64)) {
      return p::fail(Code::kCommonInvalidArgument, "code must contain 1 to 64 bytes");
    }
    if (kind != "maintenance" && (body.value("name", "").empty() || body.value("name", "").size() > 200)) {
      return p::fail(Code::kCommonInvalidArgument, "name must contain 1 to 200 bytes");
    }
    if (kind == "device_type") {
      if (auto result = validate_type(body); !result) {
        return std::unexpected(result.error());
      }
    }
    if (kind == "device") {
      if (!body.contains("deviceTypeId") || !body.contains("zoneId") || !body.contains("siteId")
          || !body.contains("connectorId") || body.value("externalId", "").empty()
          || body.value("externalId", "").size() > 256 || body.value("sources", Json::array()).size() > 1000) {
        return p::fail(Code::kCommonInvalidArgument, "invalid device references or sources");
      }
      std::set<std::string> refs;
      for (auto& source : body["sources"]) {
        const auto ref = source.value("externalRef", "");
        if (ref.empty() || ref.size() > 256 || source.value("kind", "").empty() || !refs.insert(ref).second) {
          return p::fail(Code::kCatalogDuplicateSourceRef, "invalid or duplicate source reference");
        }
        const auto source_id =
            p::stream::uuid5("728d47ed-1c69-5809-a2df-6e8b98e48ca6", {body.at("connectorId").get<std::string>(), ref});
        if (source.contains("sourceId") && source["sourceId"] != source_id) {
          return p::fail(Code::kCommonInvalidArgument,
                         "source identity does not match connector and external reference");
        }
        source["sourceId"] = source_id;
      }
      if (body.contains("position")
          && (!std::isfinite(body["position"].value("x", 0.0)) || !std::isfinite(body["position"].value("y", 0.0)))) {
        return p::fail(Code::kCatalogPositionInvalid, "position must be finite");
      }
    }
    Resource result{.kind = std::move(kind),
                    .id = std::move(id),
                    .site = body.value("siteId", ""),
                    .parent = body.value("parentId", ""),
                    .code = body.value("code", ""),
                    .connector = body.value("connectorId", ""),
                    .external = body.value("externalId", ""),
                    .body = std::move(body)};
    if (result.kind == "device") {
      result.parent = result.body.at("zoneId");
    }
    return result;
  } catch (const std::exception&) {
    return p::fail(Code::kCommonInvalidArgument, "invalid resource fields");
  }
}

p::Result<std::vector<Json>> import_rows(std::string_view format, std::string_view bytes) {
  try {
    std::vector<Json> result;
    if (format == "json") {
      const auto body = read_json(bytes);
      if (!body.is_array() || body.size() > kMaxImportRows) {
        return p::fail(Code::kCommonInvalidArgument, "import must be a bounded JSON array");
      }
      result = body.get<std::vector<Json>>();
    } else if (format == "csv") {
      auto rows = csv(bytes);
      if (!rows || rows->empty()) {
        return p::fail(Code::kCommonInvalidArgument, "CSV header is required");
      }
      const auto& header = rows->front();
      std::set<std::string> fields;
      for (const auto& name : header) {
        if (!fields.insert(name).second
            || (name != "deviceId" && name != "deviceTypeId" && name != "zoneId" && name != "siteId"
                && name != "connectorId" && name != "externalId" && name != "name" && name != "sources"
                && name != "position" && name != "state" && name != "version")) {
          return p::fail(Code::kCommonInvalidArgument, "invalid or duplicate CSV column");
        }
      }
      for (std::size_t i = 1; i < rows->size(); ++i) {
        Json row = Json::object();
        if ((*rows)[i].size() != header.size()) {
          result.emplace_back(nullptr);
          continue;
        }
        for (std::size_t j = 0; j < header.size(); ++j) {
          const auto& value = (*rows)[i][j];
          if (!value.empty()) {
            if (header[j] == "sources" || header[j] == "position") {
              auto structured = Json::parse(value, nullptr, false);
              if (structured.is_discarded()) {
                row = nullptr;
                break;
              }
              row[header[j]] = std::move(structured);
            } else {
              row[header[j]] = value;
            }
          }
        }
        result.push_back(std::move(row));
      }
    } else {
      return p::fail(Code::kCommonInvalidArgument, "unsupported import format");
    }
    if (result.size() > kMaxImportRows) {
      return p::fail(Code::kCommonInvalidArgument, "import row limit exceeded");
    }
    return result;
  } catch (const std::exception&) {
    return p::fail(Code::kCommonInvalidArgument, "invalid import file");
  }
}

std::string export_rows(std::string_view format, const std::vector<Json>& rows) {
  if (format == "json") {
    return Json(rows).dump();
  }
  const std::vector<std::string> fields{"deviceId", "deviceTypeId", "zoneId",   "siteId", "connectorId", "externalId",
                                        "name",     "sources",      "position", "state",  "version"};
  std::string output;
  for (const auto& field : fields) {
    output += (output.empty() ? "" : ",") + field;
  }
  output += "\r\n";
  for (const auto& row : rows) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
      if (i != 0) {
        output += ',';
      }
      const auto found = row.find(fields[i]);
      std::string value;
      if (found != row.end()) {
        value = found->is_string() ? found->get<std::string>() : found->dump();
      }
      output += '"';
      for (const auto ch : value) {
        output += ch;
        if (ch == '"') {
          output += '"';
        }
      }
      output += '"';
    }
    output += "\r\n";
  }
  return output;
}
}  // namespace psim::resource_catalog
