#include "report.hpp"

#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <psim/api/v1/reports.pb.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "psim/platform/authz/policy.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::projection {
namespace {
using Json = nlohmann::json;
using Time = google::protobuf::util::TimeUtil;
constexpr auto kQuery = R"sql(SELECT GROUPING(dimension) AS total,dimension,
toUInt64(countIf(metric=1)) AS created,toUInt64(countIf(metric=3)) AS resolved,
toFloat64(if(countIf(metric=2)=0,0,sumIf(ack_ms-created_ms,metric=2)/countIf(metric=2))) AS mtta,
toFloat64(if(countIf(metric=3)=0,0,sumIf(resolved_ms-created_ms,metric=3)/countIf(metric=3))) AS mttr,
toFloat64(if(countIf(metric=3)=0,0,countIf(metric=3 AND resolution=2)/countIf(metric=3))) AS false_alarm,
toFloat64(if(countIf(metric=4)=0,0,countIf(metric=4 AND ack_ms>0 AND ack_ms<=ack_due)/countIf(metric=4))) AS ack_compliance,
toFloat64(if(countIf(metric=5)=0,0,countIf(metric=5 AND resolved_ms>0 AND resolved_ms<=resolve_due)/countIf(metric=5))) AS resolve_compliance,
toUInt64(countIf(metric=1 AND escalation_level>0)) AS escalated,
toUInt64(countIf(metric=4)) AS ack_evaluated,toUInt64(countIf(metric=5)) AS resolve_evaluated
FROM (SELECT *,activity.2 AS metric,
multiIf({grouping:UInt8}=2,site_id,{grouping:UInt8}=3,type_id,{grouping:UInt8}=4,toString(priority),
{grouping:UInt8}=5,assignee_id,{grouping:UInt8}=6,toString(toDate(fromUnixTimestamp64Milli(activity.1),'UTC')),'') AS dimension
FROM (SELECT *,arrayJoin([tuple(created_ms,toUInt8(1)),tuple(ack_ms,toUInt8(2)),tuple(resolved_ms,toUInt8(3)),
tuple(if(ack_target_ms>0 AND (ack_ms>0 OR {now:Int64}>ack_due),if(ack_ms>0,least(ack_ms,ack_due),ack_due),toInt64(0)),toUInt8(4)),
tuple(if(resolve_target_ms>0 AND ack_ms>0 AND (resolved_ms>0 OR {now:Int64}>resolve_due),if(resolved_ms>0,least(resolved_ms,resolve_due),resolve_due),toInt64(0)),toUInt8(5))]) AS activity
FROM (SELECT *,toUnixTimestamp64Milli(created_at) AS created_ms,
created_ms+ack_target_ms AS ack_due,ack_ms+resolve_target_ms AS resolve_due
FROM incident_report_current
WHERE tenant_id={psim_tenant:String} AND created_at<fromUnixTimestamp64Milli({psim_to:Int64})
AND created_at>now64(3)-INTERVAL 400 DAY
AND ({all:UInt8}=1 OR has(JSONExtract({sites:String},'Array(String)'),site_id) OR has(JSONExtract({zones:String},'Array(String)'),zone_id))
AND (empty(JSONExtract({site_ids:String},'Array(String)')) OR has(JSONExtract({site_ids:String},'Array(String)'),site_id))
AND (empty(JSONExtract({types:String},'Array(String)')) OR has(JSONExtract({types:String},'Array(String)'),type_id))
AND (empty(JSONExtract({priorities:String},'Array(UInt8)')) OR has(JSONExtract({priorities:String},'Array(UInt8)'),priority))))
WHERE activity.1>0 AND activity.1>={psim_from:Int64} AND activity.1<{psim_to:Int64})
GROUP BY dimension WITH ROLLUP ORDER BY total DESC,dimension LIMIT 10002)sql";

std::string unquote(std::string_view input) {
  std::string output;
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%') {
      unsigned int value = 0;
      if (i + 2 >= input.size()) {
        throw std::invalid_argument("encoding");
      }
      const auto part = input.substr(i + 1, 2);
      const auto parsed = std::from_chars(part.begin(), part.end(), value, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != part.end() || value == 0) {
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

std::string cell(std::string value) {
  if (!value.empty()
      && (value[0] == '=' || value[0] == '+' || value[0] == '-' || value[0] == '@' || value[0] == '\t'
          || value[0] == '\r' || value[0] == '\n')) {
    value.insert(0, "'");
  }
  std::string output = "\"";
  for (char const c : value) {
    if (c == '"') {
      output += '"';
    }
    output += c;
  }
  return output + '"';
}
}  // namespace

p::Result<api::v1::GetIncidentReportRequest> parse_request(std::string_view target) {
  try {
    Json input = Json::object();
    Json filter = Json::object();
    std::set<std::string> singles;
    const auto question = target.find('?');
    auto rest = question == std::string_view::npos ? std::string_view{} : target.substr(question + 1);
    while (!rest.empty()) {
      const auto amp = rest.find('&');
      const auto equal = rest.substr(0, amp).find('=');
      if (equal == std::string_view::npos) {
        throw std::invalid_argument("query");
      }
      auto key = unquote(rest.substr(0, equal));
      auto value = unquote(rest.substr(equal + 1, amp == std::string_view::npos ? amp : amp - equal - 1));
      if (key == "filter.siteIds" || key == "filter.site_ids" || key == "filter.incidentTypeIds"
          || key == "filter.incident_type_ids" || key == "filter.priorities") {
        key = key.substr(7);
        if (key == "site_ids") {
          key = "siteIds";
        }
        if (key == "incident_type_ids") {
          key = "incidentTypeIds";
        }
        if (!filter.contains(key)) {
          filter[key] = Json::array();
        }
        if (filter[key].size() >= 100) {
          throw std::invalid_argument("filter limit");
        }
        filter[key].push_back(value);
      } else {
        if (!singles.insert(key).second) {
          throw std::invalid_argument("duplicate field");
        }
        if (key == "filter.from" || key == "filter.to") {
          filter[key.substr(7)] = value;
        } else if (key == "grouping") {
          input[key] = value;
        } else {
          throw std::invalid_argument("unknown field");
        }
      }
      rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
    }
    input["filter"] = filter;
    api::v1::GetIncidentReportRequest parsed;
    if (!google::protobuf::util::JsonStringToMessage(input.dump(), &parsed).ok()) {
      throw std::invalid_argument("fields");
    }
    return parsed;
  } catch (const std::exception&) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid report request");
  }
}

p::Result<p::clickhouse::Read> report_query(const p::security::Principal& principal,
                                            const api::v1::GetIncidentReportRequest& request) {
  const auto& f = request.filter();
  const auto valid_time = [](const google::protobuf::Timestamp& t) {
    return t.seconds() >= 0 && t.seconds() <= 10000000000LL && t.nanos() >= 0 && t.nanos() < 1000000000;
  };
  if (!valid_time(f.from()) || !valid_time(f.to())) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid report timestamp");
  }
  const auto from = Time::TimestampToMilliseconds(f.from());
  const auto to = Time::TimestampToMilliseconds(f.to());
  if (!p::stream::uuid(principal.tenant) || !f.has_from() || !f.has_to() || f.from().seconds() < 0
      || f.to().seconds() > 10000000000LL || from >= to || to - from > 34560000000LL || f.from().nanos() % 1000000 != 0
      || f.to().nanos() % 1000000 != 0 || request.grouping() < 0 || request.grouping() > 6 || f.site_ids_size() > 100
      || f.incident_type_ids_size() > 100 || f.priorities_size() > 100) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid report interval or grouping");
  }
  Json sites = Json::array();
  Json zones = Json::array();
  Json site_ids = Json::array();
  Json types = Json::array();
  Json priorities = Json::array();
  for (const auto& id : f.site_ids()) {
    if (!p::stream::uuid(id)) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid site filter");
    }
    site_ids.push_back(id);
  }
  for (const auto& id : f.incident_type_ids()) {
    if (!p::stream::uuid(id)) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid type filter");
    }
    types.push_back(id);
  }
  for (const auto priority : f.priorities()) {
    if (priority < 1 || priority > 4) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid priority filter");
    }
    priorities.push_back(priority);
  }
  for (const auto& scope : principal.scopes) {
    if (scope.starts_with("site:")) {
      sites.push_back(scope.substr(5));
    } else if (scope.starts_with("zone:")) {
      zones.push_back(scope.substr(5));
    } else if (scope != "*") {
      sites.push_back(scope);
      zones.push_back(scope);
    }
  }
  return p::clickhouse::Read{
      .statement = kQuery,
      .tenant = principal.tenant,
      .from_ms = from,
      .to_ms = to,
      .parameters = {{"all", principal.scopes.contains("*") ? "1" : "0"},
                     {"sites", sites.dump()},
                     {"zones", zones.dump()},
                     {"site_ids", site_ids.dump()},
                     {"types", types.dump()},
                     {"priorities", priorities.dump()},
                     {"grouping", std::to_string(request.grouping())},
                     {"now", std::to_string(Time::TimestampToMilliseconds(Time::GetCurrentTime()))}}};
}

p::Result<api::v1::GetIncidentReportResponse> report(const p::clickhouse::ResultSet& result, bool grouped) {
  if (result.rows > 10001) {
    return p::fail(p::ErrorCode::kCommonRateLimited, "too many report groups");
  }
  api::v1::GetIncidentReportResponse output;
  *output.mutable_generated_at() = Time::GetCurrentTime();
  for (const auto& b : result.blocks) {
    for (std::size_t row = 0; row < b.GetRowCount(); ++row) {
      const bool total = b[0]->AsStrict<::clickhouse::ColumnUInt64>()->At(row) != 0;
      if (!total && !grouped) {
        continue;
      }
      auto* m = total ? output.mutable_totals() : output.add_rows();
      m->set_group(total ? "" : std::string(b[1]->AsStrict<::clickhouse::ColumnString>()->At(row)));
      m->set_incidents_created(b[2]->AsStrict<::clickhouse::ColumnUInt64>()->At(row));
      m->set_incidents_resolved(b[3]->AsStrict<::clickhouse::ColumnUInt64>()->At(row));
      *m->mutable_mtta() = Time::MillisecondsToDuration(
          static_cast<std::int64_t>(std::llround(b[4]->AsStrict<::clickhouse::ColumnFloat64>()->At(row))));
      *m->mutable_mttr() = Time::MillisecondsToDuration(
          static_cast<std::int64_t>(std::llround(b[5]->AsStrict<::clickhouse::ColumnFloat64>()->At(row))));
      m->set_false_alarm_rate(b[6]->AsStrict<::clickhouse::ColumnFloat64>()->At(row));
      m->set_ack_sla_compliance(b[7]->AsStrict<::clickhouse::ColumnFloat64>()->At(row));
      m->set_resolve_sla_compliance(b[8]->AsStrict<::clickhouse::ColumnFloat64>()->At(row));
      m->set_escalated(b[9]->AsStrict<::clickhouse::ColumnUInt64>()->At(row));
      m->set_ack_sla_evaluated(b[10]->AsStrict<::clickhouse::ColumnUInt64>()->At(row));
      m->set_resolve_sla_evaluated(b[11]->AsStrict<::clickhouse::ColumnUInt64>()->At(row));
    }
  }
  if (!output.has_totals()) {
    output.mutable_totals();
  }
  return output;
}

std::string csv(const api::v1::GetIncidentReportResponse& value) {
  std::string out =
      "row_kind,group,incidents_created,incidents_resolved,mtta_seconds,mttr_seconds,false_alarm_rate,ack_sla_"
      "compliance,resolve_sla_compliance,escalated,ack_sla_evaluated,resolve_sla_evaluated\r\n";
  const auto append = [&out](const api::v1::IncidentMetrics& m, const char* kind) {
    out += std::string(kind) + "," + cell(m.group()) + "," + std::to_string(m.incidents_created()) + ","
           + std::to_string(m.incidents_resolved()) + ",";
    out += std::to_string(static_cast<double>(Time::DurationToMilliseconds(m.mtta())) / 1000) + ",";
    out += std::to_string(static_cast<double>(Time::DurationToMilliseconds(m.mttr())) / 1000) + ",";
    out += std::to_string(m.false_alarm_rate()) + "," + std::to_string(m.ack_sla_compliance()) + ","
           + std::to_string(m.resolve_sla_compliance()) + ",";
    out += std::to_string(m.escalated()) + "," + std::to_string(m.ack_sla_evaluated()) + ","
           + std::to_string(m.resolve_sla_evaluated()) + "\r\n";
  };
  append(value.totals(), "total");
  for (const auto& row : value.rows()) {
    append(row, "group");
  }
  return out;
}

Response Api::problem(const p::Error& error) {
  const auto& info = p::info(error.code());
  Response response(static_cast<http::status>(info.http_status), 11);
  response.set(http::field::content_type, "application/problem+json");
  response.body() =
      Json{{"type", "about:blank"}, {"title", info.title}, {"status", info.http_status}, {"code", error.name()}}.dump();
  return response;
}

boost::asio::awaitable<p::Result<Response>> Api::handle(Request request, p::security::Principal principal) {
  const std::string target(request.target());
  const auto path = target.substr(0, target.find('?'));
  const bool exporting = path == "/v1/reports/incidents:export";
  if (request.method() != http::verb::get || (path != "/v1/reports/incidents" && !exporting)) {
    co_return p::fail(p::ErrorCode::kCommonNotFound, "unknown report endpoint");
  }
  const std::string action = exporting ? "report.export" : "report.read";
  auto allowed = p::authz::decide(principal, action, {principal.tenant}, std::chrono::system_clock::now());
  if (!allowed && (allowed.error().code() != p::ErrorCode::kAuthScopeDenied || principal.scopes.empty())) {
    auto saved = co_await audit_(principal, action, true);
    if (!saved) {
      co_return std::unexpected(saved.error());
    }
    co_return std::unexpected(allowed.error());
  }
  auto parsed = parse_request(target);
  if (!parsed) {
    co_return std::unexpected(parsed.error());
  }
  auto query = report_query(principal, *parsed);
  if (!query) {
    co_return std::unexpected(query.error());
  }
  auto rows = co_await pool_->read(std::move(*query));
  if (!rows) {
    co_return std::unexpected(rows.error());
  }
  auto value = report(*rows, parsed->grouping() > api::v1::REPORT_GROUPING_NONE);
  if (!value) {
    co_return std::unexpected(value.error());
  }
  auto saved = co_await audit_(principal, action, false);
  if (!saved) {
    co_return std::unexpected(saved.error());
  }
  Response response(http::status::ok, 11);
  response.set(http::field::content_type, exporting ? "text/csv; charset=utf-8" : "application/json");
  if (exporting) {
    response.set(http::field::content_disposition, "attachment; filename=incident-report.csv");
  }
  response.body() = exporting ? csv(*value) : p::stream::json(*value);
  co_return response;
}
}  // namespace psim::projection
