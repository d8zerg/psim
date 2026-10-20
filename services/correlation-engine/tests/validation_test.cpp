#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <set>
#include <string>

#include "psim/common/v1/types.pb.h"
#include "psim/processing/v1/rules.pb.h"
#include "rules.hpp"

namespace psim::correlation::rules {
namespace {
std::set<std::string> taxonomy() {
  return {"generic.alarm", "intrusion.zone.alarm", "intrusion.zone.restore"};
}

Rule sample() {
  Rule value;
  value.set_rule_id("11111111-1111-4111-8111-111111111111");
  value.set_version(1);
  value.set_name("Alarm");
  value.set_state(processing::v1::RULE_STATE_DRAFT);
  value.set_correlation_scope(processing::v1::CORRELATION_SCOPE_ZONE);
  value.mutable_filter()->add_types("intrusion.*");
  value.mutable_single();
  value.mutable_output()->set_incident_type_id(value.rule_id());
  value.mutable_output()->set_priority(common::v1::PRIORITY_P2);
  return value;
}

TEST(RuleValidation, CompilesAllKindsAndTypedOutput) {
  auto rule = sample();
  rule.mutable_output()->set_priority_expression("event.severity >= 4 ? 1 : 2");
  rule.mutable_output()->set_summary_template("Alarm ${event.zone_id}: ${rule.name}");
  ASSERT_TRUE(compile(rule, taxonomy()).compiled);
  rule.mutable_window()->set_seconds(60);
  for (const bool sequence : {false, true}) {
    for (int index = 0; index < 2; ++index) {
      auto* part = sequence ? rule.mutable_sequence()->add_steps() : rule.mutable_conjunction()->add_parts();
      *part = rule.filter();
    }
    EXPECT_TRUE(compile(rule, taxonomy()).compiled);
  }
  rule.mutable_threshold()->set_count(3);
  rule.mutable_threshold()->set_distinct_by("attributes.channel");
  EXPECT_TRUE(compile(rule, taxonomy()).compiled);
  rule.mutable_output()->clear_priority_expression();
  *rule.mutable_absence()->mutable_expected() = rule.filter();
  EXPECT_TRUE(compile(rule, taxonomy()).compiled);
  *rule.mutable_absence()->mutable_trigger() = rule.filter();
  EXPECT_TRUE(compile(rule, taxonomy()).compiled);
}

TEST(RuleValidation, RejectsInvalidMetadataAndOutput) {
  EXPECT_FALSE(compile(Rule{}, taxonomy()).compiled);
  auto rule = sample();
  rule.set_rule_id("invalid");
  rule.set_version(0);
  rule.set_name(std::string(201, 'a'));
  rule.set_description(std::string(4001, 'a'));
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): unknown protobuf enum input
  rule.set_state(static_cast<processing::v1::RuleState>(100));
  rule.set_correlation_scope(processing::v1::CORRELATION_SCOPE_UNSPECIFIED);
  rule.mutable_output()->set_incident_type_id("invalid");
  rule.mutable_output()->set_priority(common::v1::PRIORITY_UNSPECIFIED);
  EXPECT_GE(compile(rule, taxonomy()).violations.size(), 7);
  for (const auto* priority : {"5", "0", "event.severity", "1 / 0", "'high'"}) {
    rule = sample();
    rule.mutable_output()->set_priority_expression(priority);
    EXPECT_FALSE(compile(rule, taxonomy()).compiled) << priority;
  }
  for (const auto* summary : {"${event.severity}", "${event.zone_id", "${unknown}"}) {
    rule = sample();
    rule.mutable_output()->set_summary_template(summary);
    EXPECT_FALSE(compile(rule, taxonomy()).compiled);
  }
  rule = sample();
  rule.mutable_output()->set_summary_template(std::string(1025, 'x'));
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
  rule = sample();
  rule.mutable_output()->set_grouping_scope(processing::v1::CORRELATION_SCOPE_DEVICE);
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
}

TEST(RuleValidation, RejectsInvalidFiltersDurationsAndKinds) {
  auto rule = sample();
  rule.mutable_filter()->set_condition("unknown == true");
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
  rule = sample();
  rule.mutable_filter()->add_types("unknown.*");
  rule.mutable_filter()->add_site_ids("not-a-uuid");
  rule.mutable_filter()->add_zone_ids("invalid");
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): unknown protobuf enum input
  rule.mutable_filter()->set_min_severity(static_cast<common::v1::Severity>(100));
  EXPECT_GE(compile(rule, taxonomy()).violations.size(), 4);
  rule = sample();
  rule.mutable_filter()->clear_types();
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
  rule = sample();
  rule.mutable_window()->set_seconds(std::numeric_limits<std::int64_t>::max());
  rule.mutable_suppression()->set_seconds(-1);
  rule.mutable_allowed_lateness()->set_nanos(1);
  EXPECT_GE(compile(rule, taxonomy()).violations.size(), 3);
  rule = sample();
  rule.mutable_sequence()->add_steps()->add_types("generic.alarm");
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
  rule = sample();
  rule.mutable_window()->set_seconds(60);
  rule.mutable_threshold()->set_count(1001);
  rule.mutable_threshold()->set_distinct_by("unknown");
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
  rule = sample();
  rule.mutable_window()->set_seconds(60);
  *rule.mutable_absence()->mutable_expected() = rule.filter();
  rule.mutable_filter()->set_condition("event.severity > 1");
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
}

TEST(RuleDocument, ParsesJsonAndYamlAndRejectsExcessiveInputs) {
  EXPECT_TRUE(parse_document("{\"name\":\"Alarm\",\"single\":{}}"));
  EXPECT_TRUE(parse_document("name: Alarm\nsingle: {}\n", true));
  EXPECT_FALSE(parse_document("{\"unexpected\":true}"));
  EXPECT_FALSE(parse_document(""));
  EXPECT_FALSE(parse_document(std::string(65537, 'x')));
  for (const auto* document :
       {"[", "name: A\nname: B", "name: &x Alarm", "a: &x [*x]", "name: A\n---\nname: B", "unknown: true"}) {
    EXPECT_FALSE(parse_document(document, true)) << document;
  }
  EXPECT_FALSE(parse_document(std::string(100, '[') + "0" + std::string(100, ']'), true));
}

TEST(RuleValidation, TimerOutputsCannotInferEventValuesFromInventoryDefaults) {
  auto rule = sample();
  rule.mutable_filter()->set_condition("matches.count > 1");
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
  rule.mutable_filter()->clear_condition();
  rule.mutable_window()->set_seconds(60);
  *rule.mutable_absence()->mutable_expected() = rule.filter();
  rule.mutable_output()->set_priority_expression("event.severity > 1 ? 1 : 2");
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
  rule.mutable_output()->set_priority_expression("matches.count == 0 ? 1 : 2");
  EXPECT_TRUE(compile(rule, taxonomy()).compiled);
  rule.mutable_output()->set_summary_template("${event.type}");
  EXPECT_FALSE(compile(rule, taxonomy()).compiled);
}
}  // namespace
}  // namespace psim::correlation::rules
