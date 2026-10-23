#include "search.hpp"

#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <charconv>
#include <chrono>
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
#include <vector>

#include "google/protobuf/timestamp.pb.h"
#include "psim/api/v1/events.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"

namespace psim::event_history {
namespace {
using Time = google::protobuf::util::TimeUtil;
using Code = p::ErrorCode;
constexpr auto kEvents =
    R"sql(SELECT event_id,argMax(payload,(expires_at,significant)) AS payload,toUnixTimestamp64Milli(occurred_at) AS at_ms
FROM (SELECT tenant_id,event_id,site_id,zone_id,device_id,type,severity,significant,payload,occurred_at,expires_at FROM events FINAL
UNION ALL SELECT tenant_id,event_id,site_id,zone_id,device_id,type,severity,significant,payload,occurred_at,expires_at FROM retained_events FINAL)
WHERE tenant_id={psim_tenant:String} AND occurred_at>=fromUnixTimestamp64Milli({psim_from:Int64})
AND occurred_at<fromUnixTimestamp64Milli({psim_to:Int64}) AND expires_at>now64(3)
AND ({all:UInt8}=1 OR has(JSONExtract({sites:String},'Array(String)'),site_id) OR has(JSONExtract({zones:String},'Array(String)'),zone_id))
AND (empty(JSONExtract({site_ids:String},'Array(String)')) OR has(JSONExtract({site_ids:String},'Array(String)'),site_id))
AND (empty(JSONExtract({zone_ids:String},'Array(String)')) OR has(JSONExtract({zone_ids:String},'Array(String)'),zone_id))
AND (empty(JSONExtract({device_ids:String},'Array(String)')) OR has(JSONExtract({device_ids:String},'Array(String)'),device_id))
AND (empty(JSONExtract({types:String},'Array(String)')) OR arrayExists(t -> if(endsWith(t,'.*'),startsWith(type,substring(t,1,length(t)-1)),type=t),JSONExtract({types:String},'Array(String)')))
AND severity>={severity:UInt8} AND ({significant:UInt8}=0 OR significant=1)
AND (empty(JSONExtract({ids:String},'Array(String)')) OR has(JSONExtract({ids:String},'Array(String)'),event_id))
AND (occurred_at,event_id)<(fromUnixTimestamp64Milli({cursor_time:Int64}),{cursor_id:String})
GROUP BY event_id,occurred_at ORDER BY occurred_at DESC,event_id DESC LIMIT {limit:UInt64})sql";
constexpr auto kStatistics = R"sql(SELECT rule_id,rule_version,count() AS signals,sum(length(event_ids)) AS evidence,
toUnixTimestamp64Milli(min(occurred_at)) AS first_ms,toUnixTimestamp64Milli(max(occurred_at)) AS last_ms
FROM signal_facts WHERE tenant_id={psim_tenant:String}
AND occurred_at>=fromUnixTimestamp64Milli({psim_from:Int64}) AND occurred_at<fromUnixTimestamp64Milli({psim_to:Int64})
AND expires_at>now64(3) AND rule_id!=''
AND ({all:UInt8}=1 OR has(JSONExtract({sites:String},'Array(String)'),site_id) OR has(JSONExtract({zones:String},'Array(String)'),zone_id))
AND (empty(JSONExtract({site_ids:String},'Array(String)')) OR has(JSONExtract({site_ids:String},'Array(String)'),site_id))
AND (empty(JSONExtract({zone_ids:String},'Array(String)')) OR has(JSONExtract({zone_ids:String},'Array(String)'),zone_id))
AND (empty(JSONExtract({rule_ids:String},'Array(String)')) OR has(JSONExtract({rule_ids:String},'Array(String)'),rule_id))
AND (rule_id,rule_version)>({cursor_rule:String},{cursor_version:UInt32})
GROUP BY rule_id,rule_version ORDER BY rule_id,rule_version LIMIT {limit:UInt64})sql";
constexpr auto kReferences = R"sql(SELECT DISTINCT event_id FROM event_signals WHERE tenant_id={psim_tenant:String}
AND occurred_at>=fromUnixTimestamp64Milli({psim_from:Int64}) AND occurred_at<fromUnixTimestamp64Milli({psim_to:Int64})
AND expires_at>now64(3) AND event_id>{cursor_id:String}
AND ({all:UInt8}=1 OR has(JSONExtract({sites:String},'Array(String)'),site_id) OR has(JSONExtract({zones:String},'Array(String)'),zone_id))
AND signal_id IN
(SELECT signal_id FROM incident_signals WHERE tenant_id={psim_tenant:String} AND incident_id={incident:String}
AND occurred_at>=fromUnixTimestamp64Milli({psim_from:Int64}) AND occurred_at<fromUnixTimestamp64Milli({psim_to:Int64})
AND expires_at>now64(3)
AND ({all:UInt8}=1 OR has(JSONExtract({sites:String},'Array(String)'),site_id) OR has(JSONExtract({zones:String},'Array(String)'),zone_id)))
ORDER BY event_id LIMIT {limit:UInt64})sql";

std::string hex(std::string_view value) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string out;
  for (const char byte : value) {
    const auto c = static_cast<unsigned char>(byte);
    out += kDigits[c >> 4];
    out += kDigits[c & 15];
  }
  return out;
}

Json cursor(const Json& input) {
  const auto value = input.value("pageToken", "");
  if (value.empty()) {
    return Json::object();
  }
  if (value.size() > 4096 || value.size() % 2 != 0) {
    throw std::invalid_argument("cursor");
  }
  std::string text;
  for (std::size_t i = 0; i < value.size(); i += 2) {
    unsigned int c = 0;
    const auto parsed = std::from_chars(std::string_view(value).substr(i, 2).begin(),
                                        std::string_view(value).substr(i, 2).end(), c, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != std::string_view(value).substr(i, 2).end()) {
      throw std::invalid_argument("cursor");
    }
    text += static_cast<char>(c);
  }
  return Json::parse(text);
}

std::string binding(const p::security::Principal& principal, std::string_view operation, std::string_view id,
                    Json input) {
  input.erase("pageToken");
  const Json scopes = principal.scopes;
  return p::stream::sha256(principal.tenant + ":" + principal.subject + ":" + scopes.dump() + ":"
                           + std::string(operation) + ":" + std::string(id) + ":" + input.dump());
}

std::int64_t millis(const Json& input, const char* name, std::int64_t fallback) {
  if (!input.contains(name)) {
    return fallback;
  }
  google::protobuf::Timestamp t;
  if (!Time::FromString(input.at(name).get<std::string>(), &t) || t.seconds() < 0 || t.seconds() > 10000000000LL) {
    throw std::invalid_argument("time");
  }
  return Time::TimestampToMilliseconds(t);
}

void validate_ids(const Json& input, const char* field) {
  if (!input.contains(field)) {
    return;
  }
  if (!input.at(field).is_array() || input.at(field).size() > 100) {
    throw std::invalid_argument("filter size");
  }
  for (const auto& id : input.at(field)) {
    if (!p::stream::uuid(id.get<std::string>())) {
      throw std::invalid_argument("filter identity");
    }
  }
}
}  // namespace

p::Result<p::clickhouse::Read> search_query(const p::security::Principal& principal, std::string_view operation,
                                            std::string_view id, const Json& input) {
  try {
    const bool required = operation == "search" || operation == "statistics";
    if ((required && (!input.contains("from") || !input.contains("to"))) || (!id.empty() && !p::stream::uuid(id))) {
      throw std::invalid_argument("identity or interval");
    }
    const auto token = cursor(input);
    const auto now = Time::TimestampToMilliseconds(Time::GetCurrentTime());
    const auto from = millis(input, "from", token.value("from", std::max<std::int64_t>(0, now - 7776000000LL)));
    const auto to = millis(input, "to", token.value("to", now + 1));
    if (from >= to || (required && to - from > 2678400000LL) || to - from > 315360000000LL) {
      throw std::invalid_argument("interval limit");
    }
    const auto size = input.value("pageSize", 100) == 0 ? 100 : input.value("pageSize", 100);
    if (size < 1 || size > 1000) {
      throw std::invalid_argument("page limit");
    }
    if (!token.empty() && token.at("binding") != binding(principal, operation, id, input)) {
      throw std::invalid_argument("cursor binding");
    }
    for (const auto* field : {"siteIds", "zoneIds", "deviceIds", "ruleIds"}) {
      validate_ids(input, field);
    }
    if (input.contains("types")) {
      if (!input["types"].is_array() || input["types"].size() > 100) {
        throw std::invalid_argument("types");
      }
      for (const auto& t : input["types"]) {
        if (t.get<std::string>().size() > 128) {
          throw std::invalid_argument("type");
        }
      }
    }
    Json sites = Json::array();
    Json zones = Json::array();
    const auto scope = p::authz::scope_filter(principal);
    sites = scope.sites;
    zones = scope.zones;
    std::string statement = kEvents;
    if (operation == "statistics") {
      statement = kStatistics;
    }
    if (operation == "incident") {
      statement = kReferences;
    }
    p::clickhouse::Read query{.statement = statement, .tenant = principal.tenant, .from_ms = from, .to_ms = to};
    auto& params = query.parameters;
    params = {{"all", p::authz::all_scopes(principal) ? "1" : "0"},
              {"sites", sites.dump()},
              {"zones", zones.dump()},
              {"site_ids", input.value("siteIds", Json::array()).dump()},
              {"zone_ids", input.value("zoneIds", Json::array()).dump()},
              {"device_ids", input.value("deviceIds", Json::array()).dump()},
              {"types", input.value("types", Json::array()).dump()},
              {"rule_ids", input.value("ruleIds", Json::array()).dump()},
              {"significant", input.value("significantOnly", false) ? "1" : "0"},
              {"severity", "0"},
              {"ids", operation == "get" ? Json::array({id}).dump() : "[]"},
              {"limit", std::to_string(size + 1)},
              {"cursor_time", std::to_string(token.value("time", to))},
              {"cursor_id", token.value("id", operation == "incident" ? "" : "~")},
              {"cursor_rule", token.value("rule", "")},
              {"cursor_version", std::to_string(token.value("version", 0))},
              {"incident", std::string(id)}};
    if (input.contains("minSeverity")) {
      common::v1::Severity severity{};
      if (!common::v1::Severity_Parse(input["minSeverity"].get<std::string>(), &severity)) {
        throw std::invalid_argument("severity");
      }
      params["severity"] = std::to_string(static_cast<int>(severity));
    }
    if (!token.empty() && (token.at("from") != from || token.at("to") != to)) {
      throw std::invalid_argument("cursor interval");
    }
    return query;
  } catch (const std::exception&) {
    return p::fail(Code::kCommonInvalidArgument, "invalid history filters, interval or cursor");
  }
}

boost::asio::awaitable<p::Result<Json>> Search::read(p::security::Principal principal, std::string operation,
                                                     std::string id, Json input) {
  auto allowed = p::authz::decide(principal, operation == "statistics" ? "rule.statistics.read" : "event.read",
                                  {principal.tenant}, std::chrono::system_clock::now());
  if (!allowed && allowed.error().code() != Code::kAuthScopeDenied) {
    if (denial_) {
      auto saved = co_await denial_(principal, operation);
      if (!saved) {
        co_return std::unexpected(saved.error());
      }
    }
    co_return std::unexpected(allowed.error());
  }
  auto query = search_query(principal, operation, id, input);
  if (!query) {
    co_return std::unexpected(query.error());
  }
  auto result = co_await pool_->read(*query);
  if (!result) {
    co_return std::unexpected(result.error());
  }
  const auto size = static_cast<std::size_t>(input.value("pageSize", 100) == 0 ? 100 : input.value("pageSize", 100));
  Json output = {{operation == "statistics" ? "statistics" : "events", Json::array()}};
  Json next = {{"binding", binding(principal, operation, id, input)}, {"from", query->from_ms}, {"to", query->to_ms}};
  std::size_t count = 0;
  std::vector<std::string> references;
  for (const auto& block : result->blocks) {
    for (std::size_t row = 0; row < block.GetRowCount() && count < size; ++row, ++count) {
      const auto identity = std::string(block[0]->AsStrict<::clickhouse::ColumnString>()->At(row));
      if (operation == "statistics") {
        const auto version = block[1]->AsStrict<::clickhouse::ColumnUInt32>()->At(row);
        api::v1::RuleStatistic statistic;
        statistic.set_rule_id(identity);
        statistic.set_rule_version(version);
        statistic.set_signal_count(block[2]->AsStrict<::clickhouse::ColumnUInt64>()->At(row));
        statistic.set_event_reference_count(block[3]->AsStrict<::clickhouse::ColumnUInt64>()->At(row));
        *statistic.mutable_first_fired_at() =
            Time::MillisecondsToTimestamp(block[4]->AsStrict<::clickhouse::ColumnInt64>()->At(row));
        *statistic.mutable_last_fired_at() =
            Time::MillisecondsToTimestamp(block[5]->AsStrict<::clickhouse::ColumnInt64>()->At(row));
        output["statistics"].push_back(Json::parse(p::stream::json(statistic)));
        next["rule"] = identity;
        next["version"] = version;
      } else if (operation == "incident") {
        references.push_back(identity);
        next["id"] = identity;
      } else {
        processing::v1::EventRecord record;
        if (!record.ParseFromString(std::string(block[1]->AsStrict<::clickhouse::ColumnString>()->At(row)))) {
          co_return p::fail(Code::kCommonInternal, "invalid stored event");
        }
        api::v1::StoredEvent event;
        event.set_event_id(identity);
        *event.mutable_occurred_at() = record.envelope().occurred_at();
        *event.mutable_received_at() = record.envelope().received_at();
        event.set_source_seq(record.envelope().sequence());
        *event.mutable_event() = record.event();
        output["events"].push_back(Json::parse(p::stream::json(event)));
        next["time"] = block[2]->AsStrict<::clickhouse::ColumnInt64>()->At(row);
        next["id"] = identity;
      }
    }
  }
  if (operation == "get") {
    if (output["events"].empty()) {
      co_return p::fail(Code::kCommonNotFound, "event unavailable in scope and interval");
    }
    co_return Json{{"event", output["events"].front()}};
  }
  if (operation == "incident") {
    output["unavailableEventIds"] = Json::array();
    if (!references.empty()) {
      auto bodies = *query;
      bodies.statement = kEvents;
      bodies.parameters["ids"] = Json(references).dump();
      bodies.parameters["cursor_time"] = std::to_string(query->to_ms);
      bodies.parameters["cursor_id"] = "~";
      bodies.parameters["limit"] = "1001";
      auto found = co_await pool_->read(std::move(bodies));
      if (!found) {
        co_return std::unexpected(found.error());
      }
      std::set<std::string> available;
      for (const auto& block : found->blocks) {
        for (std::size_t row = 0; row < block.GetRowCount(); ++row) {
          processing::v1::EventRecord record;
          if (!record.ParseFromString(std::string(block[1]->AsStrict<::clickhouse::ColumnString>()->At(row)))) {
            co_return p::fail(Code::kCommonInternal, "invalid stored evidence");
          }
          api::v1::StoredEvent event;
          event.set_event_id(record.envelope().message_id());
          *event.mutable_occurred_at() = record.envelope().occurred_at();
          *event.mutable_received_at() = record.envelope().received_at();
          event.set_source_seq(record.envelope().sequence());
          *event.mutable_event() = record.event();
          available.insert(event.event_id());
          output["events"].push_back(Json::parse(p::stream::json(event)));
        }
      }
      for (const auto& reference : references) {
        if (!available.contains(reference)) {
          output["unavailableEventIds"].push_back(reference);
        }
      }
    }
  }
  if (result->rows > size) {
    output["nextPageToken"] = hex(next.dump());
  }
  co_return output;
}
}  // namespace psim::event_history
