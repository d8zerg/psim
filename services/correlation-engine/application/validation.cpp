#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/common/v1/types.pb.h"
#include "psim/domain/processing/expression.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/rules.pb.h"
#include "rules.hpp"

namespace psim::correlation::rules {
namespace {
using Code = p::ErrorCode;
using Time = google::protobuf::util::TimeUtil;
using Json = nlohmann::json;

class BoundedYaml final : public YAML::EventHandler {
 public:
  void OnDocumentStart(const YAML::Mark& /*mark*/) override { touch(); }

  void OnDocumentEnd() override {}

  void OnNull(const YAML::Mark& /*mark*/, YAML::anchor_t anchor) override { touch(anchor); }

  void OnAlias(const YAML::Mark& /*mark*/, YAML::anchor_t /*anchor*/) override {
    throw std::invalid_argument("YAML aliases are unsupported");
  }

  void OnScalar(const YAML::Mark& /*mark*/, const std::string& /*tag*/, YAML::anchor_t anchor,
                const std::string& /*value*/) override {
    touch(anchor);
  }

  void OnSequenceStart(const YAML::Mark& /*mark*/, const std::string& /*tag*/, YAML::anchor_t anchor,
                       YAML::EmitterStyle::value /*style*/) override {
    start(anchor);
  }

  void OnSequenceEnd() override { --depth_; }

  void OnMapStart(const YAML::Mark& /*mark*/, const std::string& /*tag*/, YAML::anchor_t anchor,
                  YAML::EmitterStyle::value /*style*/) override {
    start(anchor);
  }

  void OnMapEnd() override { --depth_; }

  void OnAnchor(const YAML::Mark& /*mark*/, const std::string& /*value*/) override {
    throw std::invalid_argument("YAML anchors are unsupported");
  }

 private:
  void touch(YAML::anchor_t anchor = 0) {
    if (++nodes_ > 4096 || anchor != 0) {
      throw std::invalid_argument("YAML node or anchor limit");
    }
  }

  void start(YAML::anchor_t anchor) {
    touch(anchor);
    if (++depth_ > 32) {
      throw std::invalid_argument("YAML depth limit");
    }
  }

  unsigned int depth_ = 0;
  std::size_t nodes_ = 0;
};

Json json(const YAML::Node& node, std::size_t& budget, unsigned int depth = 0) {
  if (budget == 0 || depth > 32) {
    throw std::invalid_argument("document node/depth limit exceeded");
  }
  --budget;
  if (node.IsNull()) {
    return nullptr;
  }
  if (node.IsSequence()) {
    Json value = Json::array();
    for (const auto& child : node) {
      value.push_back(json(child, budget, depth + 1));
    }
    return value;
  }
  if (node.IsMap()) {
    Json value = Json::object();
    for (const auto& entry : node) {
      const auto key = entry.first.as<std::string>();
      if (value.contains(key)) {
        throw std::invalid_argument("duplicate YAML field");
      }
      value[key] = json(entry.second, budget, depth + 1);
    }
    return value;
  }
  const auto& text = node.Scalar();
  if (node.Tag() == "!") {
    return text;
  }
  if (text == "true") {
    return true;
  }
  if (text == "false") {
    return false;
  }
  auto scalar = Json::parse(text, nullptr, false);
  return !scalar.is_discarded() && scalar.is_number() ? scalar : Json(text);
}

std::optional<expr::Expression> expression(std::string_view text, expr::ValueType type, const std::string& field,
                                           std::vector<Violation>& errors) {
  auto compiled = expr::Expression::compile(text, type, expression_schema());
  if (!compiled) {
    errors.push_back({field, compiled.error()});
    return {};
  }
  return std::move(*compiled);
}

bool identifier(std::string_view value) {
  return p::stream::uuid(value);
}

bool taxon(std::string_view value, const std::set<std::string>& types) {
  if (value.ends_with(".*")) {
    const auto prefix = value.substr(0, value.size() - 1);
    return std::ranges::any_of(types, [prefix](const auto& type) { return type.starts_with(prefix); });
  }
  return types.contains(std::string(value));
}

bool inventory_field(std::string_view name) {
  return name == "event.site_id" || name == "event.zone_id" || name == "event.floor_id" || name == "event.building_id"
         || name == "event.device_id" || name == "event.source_id" || name == "event.connector_id";
}

std::optional<CompiledFilter> filter(const Filter& value, const std::string& path,
                                     const std::set<std::string>& taxonomy, std::vector<Violation>& errors) {
  if (value.types().empty() || value.types_size() > 100) {
    errors.push_back({path + ".types", "1..100 taxonomy selectors required"});
  }
  for (const auto& type : value.types()) {
    if (!taxon(type, taxonomy)) {
      errors.push_back({path + ".types", "unknown taxonomy selector"});
    }
  }
  if (!common::v1::Severity_IsValid(value.min_severity())) {
    errors.push_back({path + ".minSeverity", "invalid severity"});
  }
  if (value.site_ids_size() > 100 || value.zone_ids_size() > 100) {
    errors.push_back({path, "too many locations"});
  }
  for (const auto& site : value.site_ids()) {
    if (!identifier(site)) {
      errors.push_back({path + ".siteIds", "UUID required"});
    }
  }
  for (const auto& zone : value.zone_ids()) {
    if (!identifier(zone)) {
      errors.push_back({path + ".zoneIds", "UUID required"});
    }
  }
  auto condition = expression(value.condition().empty() ? "true" : value.condition(), expr::ValueType::kBool,
                              path + ".condition", errors);
  if (!condition) {
    return {};
  }
  if (condition->fields().contains("matches.count")) {
    errors.push_back({path + ".condition", "match count is available only in output expressions"});
  }
  return CompiledFilter{.filter = value, .condition = std::move(*condition)};
}
}  // namespace

expr::ExpressionSchema expression_schema() {
  expr::ExpressionSchema schema;
  for (const auto* field : {"type", "raw_code", "site_id", "zone_id", "floor_id", "building_id", "device_id",
                            "source_id", "connector_id"}) {
    schema["event." + std::string(field)] = expr::ValueType::kString;
  }
  for (const auto* field : {"severity", "event_class", "source_epoch"}) {
    schema["event." + std::string(field)] = expr::ValueType::kInt;
  }
  schema["event.live_observation"] = expr::ValueType::kBool;
  schema["event.attributes.*"] = expr::ValueType::kString;
  schema["rule.name"] = expr::ValueType::kString;
  schema["rule.rule_id"] = expr::ValueType::kString;
  schema["rule.version"] = expr::ValueType::kInt;
  schema["matches.count"] = expr::ValueType::kInt;
  return schema;
}

p::Result<Rule> parse_document(std::string_view document, bool yaml) {
  if (document.empty() || document.size() > 65536) {
    return p::fail(Code::kProcessingRuleInvalid, "rule document limit is 65536 bytes");
  }
  try {
    std::string text(document);
    if (yaml) {
      std::istringstream input(text);
      YAML::Parser parser(input);
      BoundedYaml handler;
      if (!parser.HandleNextDocument(handler) || parser.HandleNextDocument(handler)) {
        return p::fail(Code::kProcessingRuleInvalid, "exactly one YAML document required");
      }
      std::size_t budget = 4096;
      text = json(YAML::Load(text), budget).dump();
    }
    Rule rule;
    if (!google::protobuf::util::JsonStringToMessage(text, &rule).ok()) {
      return p::fail(Code::kProcessingRuleInvalid, "invalid rule document or unknown field");
    }
    return rule;
  } catch (const std::exception&) {
    return p::fail(Code::kProcessingRuleInvalid, "invalid or excessive rule document");
  }
}

p::Result<std::set<std::string>> load_types(const std::string& file) {
  try {
    std::set<std::string> types;
    const auto root = YAML::LoadFile(file);
    for (const auto& domain : root["domains"]) {
      for (const auto& entry : domain["types"]) {
        types.insert(entry["code"].as<std::string>());
      }
    }
    if (types.empty()) {
      return p::fail(Code::kProcessingRuleInvalid, "empty taxonomy");
    }
    return types;
  } catch (const std::exception&) {
    return p::fail(Code::kProcessingRuleInvalid, "cannot load rule taxonomy");
  }
}

Validation compile(const Rule& rule, const std::set<std::string>& taxonomy, Limits limits) {
  Validation result;
  auto& errors = result.violations;
  if (!identifier(rule.rule_id())) {
    errors.push_back({"ruleId", "UUID required"});
  }
  if (rule.version() == 0) {
    errors.push_back({"version", "positive version required"});
  }
  if (rule.name().empty() || rule.name().size() > 200 || rule.description().size() > 4000) {
    errors.push_back({"name", "invalid name or description length"});
  }
  if (!processing::v1::RuleState_IsValid(rule.state()) || rule.state() == 0) {
    errors.push_back({"state", "invalid lifecycle state"});
  }
  if (!processing::v1::CorrelationScope_IsValid(rule.correlation_scope()) || rule.correlation_scope() == 0) {
    errors.push_back({"correlationScope", "scope required"});
  }
  auto duration = [&](const google::protobuf::Duration& value, const char* name) {
    if (!Time::IsDurationValid(value) || value.seconds() < 0 || value.nanos() < 0 || value.nanos() % 1000000 != 0
        || value.seconds() > limits.max_window_ms / 1000
        || Time::DurationToMilliseconds(value) > limits.max_window_ms) {
      errors.push_back({name, "duration must be nonnegative whole milliseconds within the configured bound"});
      return std::optional<std::int64_t>{};
    }
    return std::optional<std::int64_t>(Time::DurationToMilliseconds(value));
  };
  const auto window = duration(rule.window(), "window");
  duration(rule.allowed_lateness(), "allowedLateness");
  duration(rule.suppression(), "suppression");
  if (rule.spec_case() != Rule::kSingle && window && *window <= 0) {
    errors.push_back({"window", "positive window required"});
  }
  if (rule.spec_case() == Rule::SPEC_NOT_SET) {
    errors.push_back({"spec", "rule kind required"});
  }
  if (!identifier(rule.output().incident_type_id())) {
    errors.push_back({"output.incidentTypeId", "UUID required"});
  }
  if (!common::v1::Priority_IsValid(rule.output().priority()) || rule.output().priority() == 0) {
    errors.push_back({"output.priority", "priority 1..4 required"});
  }
  if (!processing::v1::CorrelationScope_IsValid(rule.output().grouping_scope())) {
    errors.push_back({"output.groupingScope", "invalid grouping scope"});
  }
  if (rule.output().grouping_scope() != 0 && rule.output().grouping_scope() < rule.correlation_scope()) {
    errors.push_back({"output.groupingScope", "grouping scope cannot be narrower than correlation scope"});
  }
  auto base = filter(rule.filter(), "filter", taxonomy, errors);
  if (!base) {
    return result;
  }
  CompiledRule compiled{.rule = rule, .filter = std::move(*base)};
  if (!rule.output().priority_expression().empty()) {
    compiled.priority =
        expression(rule.output().priority_expression(), expr::ValueType::kInt, "output.priorityExpression", errors);
  }
  auto parts = [&](const auto& values, const std::string& name) {
    if (values.size() < 2 || values.size() > 10) {
      errors.push_back({name, "2..10 filters required"});
    }
    std::size_t index = 0;
    for (const auto& value : values) {
      auto part = filter(value, name + "[" + std::to_string(index++) + "]", taxonomy, errors);
      if (part) {
        compiled.parts.push_back(std::move(*part));
      }
    }
  };
  if (rule.has_conjunction()) {
    parts(rule.conjunction().parts(), "conjunction.parts");
  }
  if (rule.has_sequence()) {
    parts(rule.sequence().steps(), "sequence.steps");
  }
  if (rule.has_threshold()) {
    if (rule.threshold().count() < 2 || rule.threshold().count() > 1000) {
      errors.push_back({"threshold.count", "count must be 2..1000"});
    }
    const auto& distinct = rule.threshold().distinct_by();
    if (!distinct.empty() && distinct != "device_id" && distinct != "source_id"
        && (!distinct.starts_with("attributes.") || distinct.size() <= 11 || distinct.size() > 267)) {
      errors.push_back({"threshold.distinctBy", "unsupported distinct field"});
    }
  }
  if (rule.has_absence()) {
    if (rule.absence().has_trigger()) {
      compiled.trigger = filter(rule.absence().trigger(), "absence.trigger", taxonomy, errors);
    }
    auto expected = filter(rule.absence().expected(), "absence.expected", taxonomy, errors);
    if (expected) {
      compiled.parts.push_back(std::move(*expected));
    }
  }
  if (compiled.priority) {
    const auto bounds = compiled.priority->integer_bounds();
    if (!bounds || bounds->first < 1 || bounds->second > 4) {
      errors.push_back({"output.priorityExpression", "priority must be statically bounded to 1..4"});
    }
  }
  if (rule.has_absence() && !rule.absence().has_trigger()) {
    const auto fields = compiled.filter.condition.fields();
    for (const auto& name : fields) {
      if (!inventory_field(name)) {
        errors.push_back({"filter.condition",
                          "timer-driven absence requires an inventory-only base condition; use expected.condition for "
                          "event values"});
        break;
      }
    }
  }
  const auto& summary = rule.output().summary_template();
  if (summary.size() > 1024) {
    errors.push_back({"output.summaryTemplate", "summary limit is 1024 bytes"});
  }
  std::size_t at = 0;
  while (at < summary.size()) {
    const auto begin = summary.find("${", at);
    if (begin == std::string::npos) {
      compiled.summary.push_back({.literal = summary.substr(at)});
      break;
    }
    if (begin > at) {
      compiled.summary.push_back({.literal = summary.substr(at, begin - at)});
    }
    const auto end = summary.find('}', begin + 2);
    if (end == std::string::npos) {
      errors.push_back({"output.summaryTemplate", "unterminated CEL interpolation"});
      break;
    }
    auto value = expression(std::string_view(summary).substr(begin + 2, end - begin - 2), expr::ValueType::kString,
                            "output.summaryTemplate", errors);
    if (value) {
      compiled.summary.push_back({.expression = std::move(value)});
    }
    at = end + 1;
  }
  if (rule.has_absence() && !rule.absence().has_trigger()) {
    auto check_inventory = [&](const expr::Expression& value, const std::string& field) {
      for (const auto& name : value.fields()) {
        if (name.starts_with("event.") && !inventory_field(name)) {
          errors.push_back({field, "timer-driven absence output requires inventory metadata or rule fields"});
          break;
        }
      }
    };
    if (compiled.priority) {
      check_inventory(*compiled.priority, "output.priorityExpression");
    }
    for (const auto& part : compiled.summary) {
      if (part.expression) {
        check_inventory(*part.expression, "output.summaryTemplate");
      }
    }
  }
  if (errors.empty()) {
    result.compiled = std::move(compiled);
  }
  return result;
}
}  // namespace psim::correlation::rules
