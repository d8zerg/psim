#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "psim/common/v1/types.pb.h"
#include "psim/domain/processing/expression.hpp"
#include "psim/processing/v1/rules.pb.h"
#include "rules.hpp"

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer entry point ABI
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer entry point ABI
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  namespace rules = psim::correlation::rules;
  namespace expr = psim::domain::processing;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer byte buffer
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const auto schema = rules::expression_schema();
  for (auto type : {expr::ValueType::kBool, expr::ValueType::kInt, expr::ValueType::kString}) {
    auto program = expr::Expression::compile(input, type, schema);
    if (program) {
      (void)program->integer_bounds();
      (void)program->fields();
      (void)program->evaluate([&schema](std::string_view field) -> std::optional<expr::Value> {
        auto found = schema.find(field);
        if (found == schema.end()) {
          return {};
        }
        if (found->second == expr::ValueType::kString) {
          return std::string("intrusion.zone.alarm");
        }
        if (found->second == expr::ValueType::kBool) {
          return true;
        }
        return std::int64_t{4};
      });
    }
  }
  const std::set<std::string> taxonomy{"generic.alarm", "intrusion.zone.alarm", "intrusion.zone.restore",
                                       "fire.detector.alarm"};
  for (const bool yaml : {false, true}) {
    auto rule = rules::parse_document(input, yaml);
    if (rule) {
      (void)rules::compile(*rule, taxonomy);
    }
  }
  rules::Rule candidate;
  candidate.set_rule_id("11111111-1111-4111-8111-111111111111");
  candidate.set_version(1);
  candidate.set_name("fuzz");
  candidate.set_state(psim::processing::v1::RULE_STATE_DRAFT);
  candidate.set_correlation_scope(psim::processing::v1::CORRELATION_SCOPE_ZONE);
  candidate.mutable_filter()->add_types("generic.alarm");
  candidate.mutable_filter()->set_condition(std::string(input.substr(0, 4001)));
  candidate.mutable_single();
  candidate.mutable_output()->set_incident_type_id(candidate.rule_id());
  candidate.mutable_output()->set_priority(psim::common::v1::PRIORITY_P2);
  (void)rules::compile(candidate, taxonomy);
  return 0;
}
