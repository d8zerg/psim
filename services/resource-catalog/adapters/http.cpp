#include "http.hpp"

#include <google/protobuf/util/json_util.h>
#include <psim/api/v1/catalog.pb.h>
#include <psim/connector/v1/administration.pb.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "configuration.hpp"
#include "file_store.hpp"
#include "model.hpp"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "repository.hpp"

namespace psim::resource_catalog {
namespace p = platform;
using Code = p::ErrorCode;

namespace {
std::string decode(std::string_view text) {
  std::string result;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%') {
      unsigned int byte = 0;
      if (i + 2 >= text.size()) {
        throw std::invalid_argument("invalid query encoding");
      }
      const auto decoded = std::from_chars(text.substr(i + 1, 2).begin(), text.substr(i + 1, 2).end(), byte, 16);
      if (decoded.ec != std::errc{} || decoded.ptr != text.substr(i + 1, 2).end() || byte == 0) {
        throw std::invalid_argument("invalid query encoding");
      }
      result += static_cast<char>(byte);
      i += 2;
    } else {
      result += text[i] == '+' ? ' ' : text[i];
    }
  }
  return result;
}

std::pair<std::string, Json> target(std::string_view text) {
  const auto question = text.find('?');
  std::string const path(text.substr(0, question));
  Json query = Json::object();
  if (question != std::string_view::npos) {
    auto rest = text.substr(question + 1);
    while (!rest.empty()) {
      const auto amp = rest.find('&');
      const auto pair = rest.substr(0, amp);
      const auto equal = pair.find('=');
      const auto key = decode(pair.substr(0, equal));
      if (query.contains(key) || equal == std::string_view::npos) {
        throw std::invalid_argument("duplicate or invalid query");
      }
      query[key] = decode(pair.substr(equal + 1));
      rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
    }
  }
  return {path, query};
}

Response json_response(Json body, std::string wrapper = "", http::status status = http::status::ok) {
  Response response(status, 11);
  response.set(http::field::content_type, "application/json");
  if (body.is_object() && body.contains("version")) {
    const auto version = body.at("version").is_string() ? body.at("version").get<std::string>()
                                                        : std::to_string(body.at("version").get<std::uint64_t>());
    response.set(http::field::etag, '"' + version + '"');
  }
  response.body() = wrapper.empty() ? body.dump() : Json{{wrapper, std::move(body)}}.dump();
  return response;
}

p::Result<std::uint64_t> number(std::string_view text) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.begin(), text.end(), value);
  if (text.empty() || result.ec != std::errc{} || result.ptr != text.end()
      || value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return p::fail(Code::kCommonInvalidArgument, "invalid unsigned integer");
  }
  return value;
}

p::Result<Mutation> mutation(const Request& request, const p::security::Principal& principal, bool update) {
  Mutation result{.principal = principal};
  const std::string key(request["Idempotency-Key"]);
  if ((!key.empty() && !canonical_uuid(key)) || (request.method() == http::verb::post && key.empty())) {
    return p::fail(Code::kCommonInvalidArgument, "UUID Idempotency-Key required for POST");
  }
  result.key = key;
  result.fingerprint = p::stream::sha256(std::string(request.method_string()) + std::string(request.target())
                                         + request.body() + std::string(request[http::field::if_match]));
  if (update) {
    std::string version(request[http::field::if_match]);
    if (version.empty()) {
      return p::fail(Code::kCommonPreconditionRequired, "If-Match required");
    }
    if (version.size() >= 2 && version.front() == '"' && version.back() == '"') {
      version = version.substr(1, version.size() - 2);
    }
    const auto parsed = number(version);
    if (!parsed || *parsed == 0 || *parsed == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return p::fail(Code::kCommonInvalidArgument, "invalid If-Match version");
    }
    result.expected = *parsed;
  }
  return result;
}

std::string singular(std::string_view collection) {
  if (collection == "locations") {
    return "location";
  }
  if (collection == "device-types") {
    return "device_type";
  }
  if (collection == "devices") {
    return "device";
  }
  if (collection == "maintenance-windows") {
    return "maintenance";
  }
  if (collection == "connectors") {
    return "connector";
  }
  return "";
}

std::string wrapper(std::string_view kind, bool list = false) {
  if (kind == "location") {
    return list ? "locations" : "location";
  }
  if (kind == "device_type") {
    return list ? "deviceTypes" : "deviceType";
  }
  if (kind == "device") {
    return list ? "devices" : "device";
  }
  if (kind == "maintenance") {
    return list ? "maintenanceWindows" : "maintenanceWindow";
  }
  return list ? "connectors" : "connector";
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

boost::asio::awaitable<p::Result<Response>> Api::handle(Request request, p::security::Principal principal,
                                                        std::string token) {
  const std::string object(request.target());
  p::Result<Response> result = p::fail(Code::kCommonInternal, "catalog request failed");
  try {
    result = co_await route(std::move(request), principal, std::move(token));
  } catch (const std::exception&) {
    result = p::fail(Code::kCommonInvalidArgument, "invalid request fields");
  }
  if (!result && (result.error().code() == Code::kAuthScopeDenied || result.error().code() == Code::kAuthForbidden)) {
    auto recorded = co_await repository_->denied(principal, object.substr(0, 512));
    if (!recorded) {
      co_return std::unexpected(recorded.error());
    }
  }
  co_return result;
}

boost::asio::awaitable<p::Result<Response>> Api::route(Request request, p::security::Principal principal,
                                                       std::string token) {
  const auto [path, query] = target(request.target());
  if (path == "/v1/settings" || path == "/v1/settings/retention" || path == "/v1/settings/processing"
      || path == "/v1/mappings" || path.starts_with("/v1/mappings/")) {
    if (!query.empty()) {
      co_return p::fail(Code::kCommonInvalidArgument, "query not supported");
    }
    const bool mapping = path.starts_with("/v1/mappings");
    std::string id;
    if (mapping) {
      if (path != "/v1/mappings") {
        id = path.substr(13);
      }
    } else {
      id = path.substr(path.find_last_of('/') + 1);
    }
    const auto kind = mapping ? "mapping" : id;
    if (request.method() == http::verb::get) {
      if (path == "/v1/settings") {
        auto retention = co_await repository_->configurations(principal, "retention", "retention");
        if (!retention) {
          co_return std::unexpected(retention.error());
        }
        auto processing = co_await repository_->configurations(principal, "processing", "processing");
        if (!processing) {
          co_return std::unexpected(processing.error());
        }
        co_return json_response({{"retention", *retention}, {"processing", *processing}});
      }
      if (!mapping) {
        co_return p::fail(Code::kCommonNotFound, "unknown settings operation");
      }
      auto result = co_await repository_->configurations(principal, kind, id);
      if (!result) {
        co_return std::unexpected(result.error());
      }
      co_return json_response(*result, id.empty() ? "mappingRuleSets" : "mappingRuleSet");
    }
    if (request.method() != http::verb::put || id.empty() || path == "/v1/settings"
        || request[http::field::content_type] != "application/json") {
      co_return p::fail(Code::kCommonInvalidArgument, "JSON PUT required");
    }
    const std::string expected(request[http::field::if_match]);
    if (expected.empty()) {
      co_return p::fail(Code::kCommonPreconditionRequired, "If-Match required");
    }
    if (expected.size() < 3 || expected.front() != '"' || expected.back() != '"') {
      co_return p::fail(Code::kCommonInvalidArgument, "quoted If-Match version required");
    }
    auto version = number(std::string_view(expected).substr(1, expected.size() - 2));
    if (!version) {
      co_return std::unexpected(version.error());
    }
    auto change = mutation(request, principal, false);
    if (!change) {
      co_return std::unexpected(change.error());
    }
    change->expected = *version;
    change->action = "publish";
    auto record = configuration(kind, id, Json::parse(request.body()), taxonomy_);
    if (!record) {
      co_return std::unexpected(record.error());
    }
    auto result = co_await repository_->configure(std::move(*record), kind, id, *change);
    if (!result) {
      co_return std::unexpected(result.error());
    }
    co_return json_response(*result, mapping ? "mappingRuleSet" : kind);
  }

  std::string_view policy_action = "catalog.read";
  if (request.method() != http::verb::get) {
    policy_action = path.starts_with("/v1/connectors") ? "connector.write" : "catalog.write";
  }
  auto access = p::authz::decide_role(principal, policy_action, std::chrono::system_clock::now());
  if (!access) {
    co_return std::unexpected(access.error());
  }
  if (!path.starts_with("/v1/")) {
    co_return p::fail(Code::kCommonNotFound, "unknown endpoint");
  }
  for (const auto& [key, value] : query.items()) {
    (void)value;
    if (key != "pageSize" && key != "pageToken" && key != "locationId" && key != "parentId" && key != "siteId"
        && key != "kind" && key != "includeArchived" && key != "state" && key != "connectorId" && key != "deviceTypeId"
        && key != "width" && key != "height" && key != "version" && key != "format" && key != "dryRun"
        && key != "certificatePem") {
      co_return p::fail(Code::kCommonInvalidArgument, "unknown query parameter");
    }
  }
  const auto page_size = number(query.value("pageSize", "100"));
  if (!page_size || *page_size > 1000) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid page size");
  }
  const auto size = static_cast<std::size_t>(*page_size == 0 ? 100 : *page_size);
  const auto after = query.value("pageToken", "");
  const auto rest = path.substr(4);
  const auto slash = rest.find('/');
  const auto collection = rest.substr(0, slash);
  const auto suffix = slash == std::string::npos ? "" : rest.substr(slash + 1);
  const auto kind = singular(collection);
  if (collection == "imports") {
    if (request.method() == http::verb::get && !suffix.empty()) {
      auto job = co_await repository_->get_import(principal, suffix);
      if (!job) {
        co_return std::unexpected(job.error());
      }
      co_return json_response(*job, "importJob");
    }
    if (request.method() == http::verb::post && suffix.empty()) {
      auto change = mutation(request, principal, false);
      if (!change) {
        co_return std::unexpected(change.error());
      }
      const auto format = query.value("format", "json");
      auto rows = co_await blocking_->run([&request, &format] { return import_rows(format, request.body()); });
      if (!rows) {
        co_return std::unexpected(rows.error());
      }
      change->action = "start";
      if (query.value("dryRun", "false") != "false" && query.value("dryRun", "false") != "true") {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid dryRun");
      }
      auto job =
          co_await repository_->start_import(*change, std::move(*rows), query.value("dryRun", "false") == "true");
      if (!job) {
        co_return std::unexpected(job.error());
      }
      co_return json_response(*job, "importJob");
    }
  }
  if (collection == "devices:export" && request.method() == http::verb::get) {
    const auto format = query.value("format", "json");
    if (format != "json" && format != "csv") {
      co_return p::fail(Code::kCommonInvalidArgument, "invalid export format");
    }
    std::vector<Json> rows;
    std::size_t exported_bytes = 0;
    std::string cursor;
    while (true) {
      auto page = co_await repository_->list(principal, "device", query, 1000, cursor);
      if (!page) {
        co_return std::unexpected(page.error());
      }
      for (auto& row : page->items) {
        exported_bytes += row.dump().size();
        if (exported_bytes > std::size_t{64} * 1024 * 1024) {
          co_return p::fail(Code::kCommonRateLimited, "export byte limit exceeded");
        }
        rows.push_back(std::move(row));
      }
      if (rows.size() > kMaxImportRows) {
        co_return p::fail(Code::kCommonRateLimited, "export row limit exceeded");
      }
      cursor = page->next;
      if (cursor.empty()) {
        break;
      }
    }
    auto bytes = co_await blocking_->run([&format, &rows] { return export_rows(format, rows); });
    if (!bytes) {
      co_return std::unexpected(bytes.error());
    }
    Response response(http::status::ok, 11);
    response.set(http::field::content_type, format == "csv" ? "text/csv" : "application/json");
    response.body() = std::move(*bytes);
    co_return response;
  }
  if (kind.empty()) {
    co_return p::fail(Code::kCommonNotFound, "unknown collection");
  }
  const auto id_end = suffix.find_first_of("/:");
  const auto id = suffix.substr(0, id_end);
  const auto action = id_end == std::string::npos ? "" : suffix.substr(id_end);
  if (!suffix.empty() && !canonical_uuid(id)) {
    co_return p::fail(Code::kCommonInvalidArgument, "invalid resource id");
  }
  if (kind == "location" && (action == "/floor-plan" || action == "/floor-plan/versions")) {
    if (request.method() == http::verb::get) {
      if (action == "/floor-plan/versions") {
        auto page = co_await repository_->files(principal, id, size, after);
        if (!page) {
          co_return std::unexpected(page.error());
        }
        co_return json_response({{"files", page->items}, {"nextPageToken", page->next}});
      }
      auto version = number(query.value("version", "0"));
      if (!version) {
        co_return std::unexpected(version.error());
      }
      auto metadata = co_await repository_->file(principal, id, *version);
      if (!metadata) {
        co_return std::unexpected(metadata.error());
      }
      auto bytes = co_await files_->get({.key = metadata->at("objectKey"),
                                         .size = std::stoull(metadata->at("size").get<std::string>()),
                                         .sha256 = metadata->at("sha256")});
      if (!bytes) {
        co_return std::unexpected(bytes.error());
      }
      Response response(http::status::ok, 11);
      response.set(http::field::content_type, metadata->at("contentType").get<std::string>());
      response.set(http::field::etag, '"' + metadata->at("sha256").get<std::string>() + '"');
      response.body() = std::move(*bytes);
      co_return response;
    }
    if (request.method() == http::verb::put && action == "/floor-plan") {
      auto change = mutation(request, principal, true);
      if (!change) {
        co_return std::unexpected(change.error());
      }
      auto floor = co_await repository_->get(principal, "location", id);
      if (!floor) {
        co_return std::unexpected(floor.error());
      }
      if (!allowed(principal, floor->at("siteId").get<std::string>(), true)) {
        co_return p::fail(Code::kAuthScopeDenied, "floor outside write scope");
      }
      if (floor->at("version") != std::to_string(change->expected)) {
        co_return p::fail(Code::kCommonConcurrentModification, "floor version changed");
      }
      const auto width = std::stod(query.value("width", "0"));
      const auto height = std::stod(query.value("height", "0"));
      if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
        co_return p::fail(Code::kCommonInvalidArgument, "plan dimensions required");
      }
      auto image = co_await blocking_->run(
          [&request] { return validate_image(request[http::field::content_type], request.body()); });
      if (!image) {
        co_return std::unexpected(image.error());
      }
      const auto file_id = p::stream::uuid7();
      const auto key = principal.tenant + "/" + file_id;
      Json metadata{{"fileId", file_id},
                    {"ownerId", id},
                    {"siteId", floor->at("siteId")},
                    {"objectKey", key},
                    {"contentType", std::string(request[http::field::content_type])},
                    {"size", std::to_string(request.body().size())},
                    {"sha256", p::stream::sha256(request.body())},
                    {"pixelWidth", image->width},
                    {"pixelHeight", image->height},
                    {"createdAt", timestamp(std::chrono::system_clock::now())}};
      auto reservation = co_await repository_->reserve_file(principal, metadata);
      if (!reservation) {
        co_return std::unexpected(reservation.error());
      }
      auto uploaded = co_await files_->put(key, std::move(request.body()));
      if (!uploaded) {
        co_return std::unexpected(uploaded.error());
      }
      if (uploaded->sha256 != metadata.at("sha256").get<std::string>()
          || uploaded->size != std::stoull(metadata.at("size").get<std::string>())) {
        co_return p::fail(Code::kCommonUnavailable, "uploaded image integrity mismatch");
      }
      change->action = "upload";
      auto saved = co_await repository_->attach_file(std::move(metadata), width, height, *change);
      if (!saved) {
        co_return std::unexpected(saved.error());
      }
      co_return json_response(*saved, "location");
    }
    co_return p::fail(Code::kCommonNotFound, "unknown floor plan operation");
  }
  if (request.method() == http::verb::get && suffix.empty()) {
    auto page = co_await repository_->list(principal, kind, query, size, after);
    if (!page) {
      co_return std::unexpected(page.error());
    }
    if (kind == "connector") {
      Json statuses = Json::array();
      for (auto& connector : page->items) {
        statuses.push_back({{"connector", std::move(connector)}});
      }
      co_return json_response({{"connectors", std::move(statuses)}, {"nextPageToken", page->next}});
    }
    co_return json_response({{wrapper(kind, true), page->items}, {"nextPageToken", page->next}});
  }
  if (request.method() == http::verb::get && !id.empty() && action.empty()) {
    auto resource = co_await repository_->get(principal, kind, id);
    if (!resource) {
      co_return std::unexpected(resource.error());
    }
    co_return json_response(*resource, wrapper(kind));
  }
  const bool create = request.method() == http::verb::post && suffix.empty();
  const bool update = request.method() == http::verb::put && !id.empty() && action.empty();
  const bool command = request.method() == http::verb::post && !id.empty() && !action.empty();
  if (!create && !update && !command) {
    co_return p::fail(Code::kCommonNotFound, "unknown resource operation");
  }
  auto change = mutation(request, principal, !create);
  if (!change) {
    co_return std::unexpected(change.error());
  }
  if (kind == "connector") {
    if (!p::authz::decide(principal, "connector.write", {principal.tenant}, std::chrono::system_clock::now())) {
      co_return p::fail(Code::kAuthScopeDenied, "connector management requires tenant scope");
    }
    connector::v1::ManageRequest management;
    management.set_request_id(change->key);
    management.set_expected_version(change->expected);
    if (create) {
      const auto body = read_json(request.body());
      if (!google::protobuf::util::JsonStringToMessage(body.dump(), management.mutable_connector()).ok()) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid connector");
      }
      management.set_action(connector::v1::REGISTRATION_ACTION_REGISTER);
      management.set_certificate_pem(query.value("certificatePem", ""));
    } else if (action == ":rotate-certificate") {
      api::v1::RotateConnectorCertificateRequest body;
      if (!google::protobuf::util::JsonStringToMessage(request.body(), &body).ok()
          || (!body.connector_id().empty() && body.connector_id() != id)) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid rotation request");
      }
      management.mutable_connector()->set_connector_id(id);
      management.set_action(connector::v1::REGISTRATION_ACTION_ROTATE);
      management.set_certificate_pem(body.certificate_pem());
      management.set_expected_fingerprint(body.new_certificate().sha256());
      if (body.has_previous_not_after()) {
        *management.mutable_previous_not_after() = body.previous_not_after();
      }
    } else if (action == ":revoke") {
      api::v1::RevokeConnectorRequest body;
      if (!google::protobuf::util::JsonStringToMessage(request.body(), &body).ok()
          || (!body.connector_id().empty() && body.connector_id() != id) || body.reason().empty()) {
        co_return p::fail(Code::kCommonInvalidArgument, "invalid revocation request");
      }
      management.mutable_connector()->set_connector_id(id);
      management.set_action(body.fingerprint().empty() ? connector::v1::REGISTRATION_ACTION_DISABLE
                                                       : connector::v1::REGISTRATION_ACTION_REVOKE);
      management.set_fingerprint(body.fingerprint());
      management.set_reason(body.reason());
    } else {
      co_return p::fail(Code::kCommonNotFound, "unknown connector operation");
    }
    auto response = co_await admin_->manage(std::move(management), std::move(token));
    if (!response) {
      co_return std::unexpected(response.error());
    }
    co_return json_response(Json::parse(p::stream::json(response->connector())), "connector");
  }
  Json body;
  if (command) {
    if (!((kind == "location" && action == ":archive") || (kind == "maintenance" && action == ":cancel"))) {
      co_return p::fail(Code::kCommonNotFound, "unknown command");
    }
    auto current = co_await repository_->get(principal, kind, id);
    if (!current) {
      co_return std::unexpected(current.error());
    }
    body = std::move(*current);
    change->action = kind == "location" ? "archive" : "cancel";
  } else {
    if (request.body().size() > 65536
        || !std::string(request[http::field::content_type]).starts_with("application/json")) {
      co_return p::fail(Code::kCommonInvalidArgument, "bounded JSON body required");
    }
    body = read_json(request.body());
    change->action = create ? "create" : "update";
  }
  auto resource = parse(kind, body);
  if (!resource) {
    co_return std::unexpected(resource.error());
  }
  if (!id.empty() && resource->id != id) {
    co_return p::fail(Code::kCommonInvalidArgument, "path and body identities differ");
  }
  auto saved = co_await repository_->change(std::move(*resource), std::move(*change));
  if (!saved) {
    co_return std::unexpected(saved.error());
  }
  co_return json_response(*saved, wrapper(kind));
}
}  // namespace psim::resource_catalog
