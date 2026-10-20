#include <google/protobuf/message.h>
#include <google/protobuf/unknown_field_set.h>
#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "psim/catalog/v1/maintenance.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/rules.pb.h"
#include "rules.hpp"

namespace psim::correlation::rules {
namespace {
using Time = google::protobuf::util::TimeUtil;
constexpr auto kTenant = "11111111-1111-4111-8111-111111111111";
constexpr auto kSite = "22222222-2222-4222-8222-222222222222";
constexpr auto kZone = "33333333-3333-4333-8333-333333333333";

Rule example() {
  Rule rule;
  rule.set_rule_id("44444444-4444-4444-8444-444444444444");
  rule.set_version(1);
  rule.set_name("Perimeter");
  rule.set_state(processing::v1::RULE_STATE_TESTING);
  rule.set_correlation_scope(processing::v1::CORRELATION_SCOPE_ZONE);
  rule.mutable_window()->set_seconds(1);
  rule.mutable_filter()->add_types("intrusion.*");
  rule.mutable_single();
  rule.mutable_output()->set_incident_type_id(rule.rule_id());
  rule.mutable_output()->set_priority(common::v1::PRIORITY_P2);
  rule.mutable_output()->set_summary_template("Alarm ${event.zone_id}");
  return rule;
}

CompiledRule compiled(const Rule& rule) {
  auto result =
      compile(rule, std::set<std::string>{"intrusion.zone.alarm", "intrusion.zone.restore", "fire.detector.alarm"});
  if (!result.compiled) {
    throw std::runtime_error("invalid test rule");
  }
  return std::move(*result.compiled);
}

Event event(std::int64_t time, std::string type = "intrusion.zone.alarm") {
  Event value;
  value.mutable_envelope()->set_message_id(p::stream::uuid5(kTenant, {std::to_string(time), type}));
  value.mutable_envelope()->set_tenant_id(kTenant);
  *value.mutable_envelope()->mutable_occurred_at() = Time::MillisecondsToTimestamp(time);
  *value.mutable_envelope()->mutable_received_at() = Time::MillisecondsToTimestamp(time + 100);
  value.mutable_event()->set_site_id(kSite);
  value.mutable_event()->set_zone_id(kZone);
  value.mutable_event()->set_device_id(kZone);
  value.mutable_event()->set_source_id(kZone);
  value.mutable_event()->set_type(std::move(type));
  return value;
}

TEST(RuleReplay, SingleIsDeterministicDeduplicatedAndTenantScoped) {
  auto rule = compiled(example());
  auto first = replay(rule, {event(1100), event(1000), event(1000)}, {}, kTenant, 1000, 3000);
  auto second = replay(rule, {event(1000), event(1100)}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_EQ(first->decisions.size(), 2);
  EXPECT_EQ(first->processed, 2);
  EXPECT_EQ(first->decisions.front().id, second->decisions.front().id);
  auto other = event(1000);
  other.mutable_envelope()->set_tenant_id(kSite);
  EXPECT_TRUE(replay(rule, {other}, {}, kTenant, 1000, 3000)->decisions.empty());
  auto conflict = event(1000);
  conflict.mutable_event()->set_raw_code("changed");
  EXPECT_FALSE(replay(rule, {event(1000), conflict}, {}, kTenant, 1000, 3000));
  EXPECT_FALSE(replay(rule, {}, {}, kTenant, 3000, 1000));
}

TEST(RuleReplay, IdentityCoversUnknownFieldsAndIgnoresMapInsertionOrder) {
  auto first = event(1000);
  auto second = event(1000);
  (*first.mutable_event()->mutable_attributes())["a"] = "1";
  (*first.mutable_event()->mutable_attributes())["b"] = "2";
  (*second.mutable_event()->mutable_attributes())["b"] = "2";
  (*second.mutable_event()->mutable_attributes())["a"] = "1";
  auto result = replay(compiled(example()), {first, second}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->processed, 1);
  Event::GetReflection()->MutableUnknownFields(&second)->AddVarint(999, 1);
  EXPECT_FALSE(replay(compiled(example()), {first, second}, {}, kTenant, 1000, 3000));
}

TEST(RuleReplay, IncidentTypesHaveSeparateGroupingKeysAndWideGroupsHaveNoZone) {
  auto rule = example();
  auto first = replay(compiled(rule), {event(1000)}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(first);
  rule.mutable_output()->set_incident_type_id(kSite);
  auto different = replay(compiled(rule), {event(1000)}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(different);
  EXPECT_NE(first->decisions.front().signal.grouping_key(), different->decisions.front().signal.grouping_key());
  rule.mutable_output()->set_grouping_scope(processing::v1::CORRELATION_SCOPE_SITE);
  auto wide = replay(compiled(rule), {event(1000)}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(wide);
  EXPECT_TRUE(wide->decisions.front().signal.zone_id().empty());
}

TEST(RuleReplay, ThresholdAndSuppressionRespectWindowBoundaries) {
  auto rule = example();
  rule.mutable_threshold()->set_count(2);
  auto hit = replay(compiled(rule), {event(1000), event(1999)}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->decisions.size(), 1);
  auto miss = replay(compiled(rule), {event(1000), event(2000)}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(miss);
  EXPECT_TRUE(miss->decisions.empty());
  rule.mutable_threshold()->set_distinct_by("device_id");
  EXPECT_TRUE(replay(compiled(rule), {event(1000), event(1100)}, {}, kTenant, 1000, 3000)->decisions.empty());
  rule.mutable_single();
  rule.mutable_suppression()->set_seconds(1);
  EXPECT_EQ(replay(compiled(rule), {event(1000), event(1500), event(2000)}, {}, kTenant, 1000, 3000)->decisions.size(),
            2);
}

TEST(RuleReplay, ConjunctionNeedsDistinctEventsAndSequenceNeedsOrder) {
  auto rule = example();
  for (const bool sequence : {false, true}) {
    auto* a = sequence ? rule.mutable_sequence()->add_steps() : rule.mutable_conjunction()->add_parts();
    a->add_types("intrusion.zone.alarm");
    auto* b = sequence ? rule.mutable_sequence()->add_steps() : rule.mutable_conjunction()->add_parts();
    b->add_types("intrusion.zone.restore");
    EXPECT_EQ(replay(compiled(rule), {event(1000), event(1100, "intrusion.zone.restore")}, {}, kTenant, 1000, 3000)
                  ->decisions.size(),
              1);
    const auto reversed =
        replay(compiled(rule), {event(1000, "intrusion.zone.restore"), event(1100)}, {}, kTenant, 1000, 3000);
    ASSERT_TRUE(reversed);
    EXPECT_EQ(reversed->decisions.size(), sequence ? 0 : 1);
  }
  rule.mutable_conjunction()->clear_parts();
  *rule.mutable_conjunction()->add_parts() = rule.filter();
  *rule.mutable_conjunction()->add_parts() = rule.filter();
  EXPECT_TRUE(replay(compiled(rule), {event(1000)}, {}, kTenant, 1000, 3000)->decisions.empty());
  EXPECT_EQ(replay(compiled(rule), {event(1000), event(1100)}, {}, kTenant, 1000, 3000)->decisions.size(), 1);
}

TEST(RuleReplay, AbsenceHasExplicitDeadlinesAndInventory) {
  auto rule = example();
  rule.mutable_absence()->mutable_trigger()->add_types("intrusion.zone.alarm");
  rule.mutable_absence()->mutable_expected()->add_types("intrusion.zone.restore");
  auto missing = replay(compiled(rule), {event(1000)}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(missing);
  ASSERT_EQ(missing->decisions.size(), 1);
  EXPECT_EQ(Time::TimestampToMilliseconds(missing->decisions.front().signal.absence_deadline()), 2000);
  EXPECT_TRUE(replay(compiled(rule), {event(1000), event(1999, "intrusion.zone.restore")}, {}, kTenant, 1000, 3000)
                  ->decisions.empty());
  EXPECT_EQ(replay(compiled(rule), {event(1000), event(2000, "intrusion.zone.restore")}, {}, kTenant, 1000, 3000)
                ->decisions.size(),
            1);
  rule.mutable_absence()->clear_trigger();
  EXPECT_FALSE(replay(compiled(rule), {}, {}, kTenant, 1000, 3500));
  Context context;
  context.subjects.push_back(event(1000));
  auto silent = replay(compiled(rule), {}, context, kTenant, 1000, 3500);
  ASSERT_TRUE(silent);
  ASSERT_EQ(silent->decisions.size(), 2);
  EXPECT_TRUE(silent->decisions.front().signal.event_ids().empty());
  EXPECT_TRUE(silent->decisions.front().signal.has_absence_deadline());
}

TEST(RuleReplay, MaintenancePreservesDecisionsAndProtectsFireAndP1) {
  Context context;
  auto& maintenance = context.maintenance.emplace_back();
  maintenance.set_maintenance_id(kSite);
  maintenance.set_site_id(kSite);
  maintenance.set_zone_id(kZone);
  *maintenance.mutable_starts_at() = Time::MillisecondsToTimestamp(1000);
  *maintenance.mutable_activated_at() = maintenance.starts_at();
  *maintenance.mutable_ends_at() = Time::MillisecondsToTimestamp(2000);
  auto rule = example();
  maintenance.mutable_suppression()->add_rule_ids(rule.rule_id());
  auto result = replay(compiled(rule), {event(1000), event(1900)}, context, kTenant, 1000, 3000);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->decisions.size(), 2);
  EXPECT_EQ(std::ranges::count_if(result->decisions, [](const auto& decision) { return decision.suppressed; }), 1);
  rule.mutable_output()->set_priority(common::v1::PRIORITY_P1);
  EXPECT_FALSE(replay(compiled(rule), {event(1000)}, context, kTenant, 1000, 3000)->decisions.front().suppressed);
  rule.mutable_output()->set_priority(common::v1::PRIORITY_P2);
  rule.mutable_filter()->clear_types();
  rule.mutable_filter()->add_types("fire.*");
  EXPECT_FALSE(replay(compiled(rule), {event(1000, "fire.detector.alarm")}, context, kTenant, 1000, 3000)
                   ->decisions.front()
                   .suppressed);
}

TEST(RuleReplay, MaintenanceUsesHistoricalPolicyAndRequiresCompleteScope) {
  auto rule = example();
  Context context;
  auto& first = context.maintenance.emplace_back();
  first.set_maintenance_id(kSite);
  first.set_version(1);
  first.set_site_id(kSite);
  first.set_zone_id(kZone);
  *first.mutable_starts_at() = Time::MillisecondsToTimestamp(1000);
  *first.mutable_activated_at() = first.starts_at();
  *first.mutable_updated_at() = first.starts_at();
  *first.mutable_ends_at() = Time::MillisecondsToTimestamp(3000);
  first.mutable_suppression()->add_rule_ids(rule.rule_id());
  auto second = first;
  second.set_version(2);
  *second.mutable_updated_at() = Time::MillisecondsToTimestamp(1500);
  second.mutable_suppression()->clear_rule_ids();
  context.maintenance.push_back(second);
  auto result = replay(compiled(rule), {event(1000), event(1500)}, context, kTenant, 1000, 3000);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->decisions.size(), 2);
  EXPECT_EQ(std::ranges::count_if(result->decisions, [](const auto& decision) { return decision.suppressed; }), 1);
  for (const auto& decision : result->decisions) {
    if (decision.suppressed) {
      ASSERT_EQ(decision.maintenance_versions.size(), 1);
      EXPECT_EQ(decision.maintenance_versions.front().second, 1);
    }
  }
  context.maintenance.erase(context.maintenance.begin());
  EXPECT_FALSE(replay(compiled(rule), {event(1000)}, context, kTenant, 1000, 3000));
  context.maintenance.front().set_device_id(kZone);
  context.maintenance.front().mutable_suppression()->add_rule_ids(rule.rule_id());
  *context.maintenance.front().mutable_updated_at() = Time::MillisecondsToTimestamp(1000);
  *rule.mutable_conjunction()->add_parts() = rule.filter();
  *rule.mutable_conjunction()->add_parts() = rule.filter();
  auto outside = event(1000);
  outside.mutable_event()->set_device_id(kSite);
  auto composite = replay(compiled(rule), {outside, event(1100)}, context, kTenant, 1000, 3000);
  ASSERT_TRUE(composite);
  ASSERT_EQ(composite->decisions.size(), 1);
  EXPECT_FALSE(composite->decisions.front().suppressed);
}

TEST(RuleReplay, MaintenanceSuppressionDoesNotStartSignalCooldown) {
  auto rule = example();
  rule.mutable_suppression()->set_seconds(10);
  Context context;
  auto& maintenance = context.maintenance.emplace_back();
  maintenance.set_maintenance_id(kSite);
  maintenance.set_site_id(kSite);
  maintenance.set_zone_id(kZone);
  *maintenance.mutable_starts_at() = Time::MillisecondsToTimestamp(1000);
  *maintenance.mutable_activated_at() = maintenance.starts_at();
  *maintenance.mutable_ends_at() = Time::MillisecondsToTimestamp(2000);
  maintenance.mutable_suppression()->add_rule_ids(rule.rule_id());
  auto result = replay(compiled(rule), {event(1000), event(2000)}, context, kTenant, 1000, 3000);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->decisions.size(), 2);
  EXPECT_EQ(std::ranges::count_if(result->decisions, [](const auto& decision) { return decision.suppressed; }), 1);
}
}  // namespace
}  // namespace psim::correlation::rules
