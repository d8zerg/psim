#include "live.hpp"

#include <gtest/gtest.h>
#include <psim/catalog/v1/maintenance.pb.h>
#include <psim/config/v1/config.pb.h>
#include <psim/processing/v1/rules.pb.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "live_support.hpp"
#include "rules.hpp"

namespace psim::correlation::live {
namespace {
using namespace test;

TEST(LiveControl, CatalogAddsDistinctDevicesAndReplacesOnlyTheUpdatedDevice) {
  constexpr auto kDevice = "55555555-5555-4555-8555-555555555555";
  Control control({"intrusion.zone.alarm"});
  catalog::v1::CatalogRecord record;
  record.mutable_envelope()->set_tenant_id(kTenant);
  auto* device = record.mutable_device();
  device->set_device_id(kDevice);
  device->set_version(1);
  device->set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
  device->add_sources()->set_source_id(kRule);
  ASSERT_TRUE(control.catalog(record));
  device->set_device_id(kSite);
  ASSERT_TRUE(control.catalog(record));
  ASSERT_EQ(control.context(kTenant).subjects.size(), 2);
  device->set_device_id(kDevice);
  device->set_version(2);
  device->mutable_sources(0)->set_source_id(kSite);
  ASSERT_TRUE(control.catalog(record));
  ASSERT_EQ(control.context(kTenant).subjects.size(), 2);
  device->set_version(3);
  device->set_state(catalog::v1::RESOURCE_STATE_DISABLED);
  ASSERT_TRUE(control.catalog(record));
  ASSERT_EQ(control.context(kTenant).subjects.size(), 1);
  EXPECT_EQ(control.context(kTenant).subjects.front().event().device_id(), kSite);
  device->set_version(1);
  device->set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
  ASSERT_TRUE(control.catalog(record));
  EXPECT_EQ(control.context(kTenant).subjects.size(), 1);
}

TEST(LiveCorrelation, SnapshotRestoresSuppressionAndDuplicateIdentity) {
  auto spec = rule();
  spec.mutable_suppression()->set_seconds(1);
  const auto compiled = compile(spec);
  State state;
  const auto first = event(1000);
  auto emitted = advance(compiled, state, &first, {}, kTenant, 1100);
  ASSERT_TRUE(emitted);
  ASSERT_EQ(emitted->decisions.size(), 1);
  State restored;
  ASSERT_TRUE(restored.ParseFromString(state.SerializeAsString()));
  auto duplicate = advance(compiled, restored, &first, {}, kTenant, 1100);
  ASSERT_TRUE(duplicate);
  EXPECT_TRUE(duplicate->decisions.empty());
  const auto suppressed = event(1500, 2);
  EXPECT_TRUE(advance(compiled, restored, &suppressed, {}, kTenant, 1600)->decisions.empty());
  const auto later = event(2000, 3);
  EXPECT_EQ(advance(compiled, restored, &later, {}, kTenant, 2100)->decisions.size(), 1);
}

TEST(LiveCorrelation, ThresholdOrdersBufferedEventsAndAgreesWithReplay) {
  auto spec = rule();
  spec.mutable_threshold()->set_count(2);
  const auto compiled = compile(spec);
  State state;
  const auto first = event(1100, 2);
  const auto second = event(1000);
  EXPECT_TRUE(advance(compiled, state, &first, {}, kTenant, 1200)->decisions.empty());
  EXPECT_TRUE(advance(compiled, state, &second, {}, kTenant, 1200)->decisions.empty());
  auto result = advance(compiled, state, nullptr, {}, kTenant, 50000);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->decisions.size(), 1);
  const auto historical = rules::replay(compiled, {first, second}, {}, kTenant, 1000, 3000);
  ASSERT_TRUE(historical);
  ASSERT_EQ(historical->decisions.size(), 1);
  EXPECT_EQ(result->decisions.front().id, historical->decisions.front().id);
  EXPECT_EQ(result->decisions.front().signal.SerializeAsString(),
            historical->decisions.front().signal.SerializeAsString());
  EXPECT_TRUE(advance(compiled, state, nullptr, {}, kTenant, 50001)->decisions.empty());
}

TEST(LiveCorrelation, ZeroSuppressionDoesNotDiscardOutOfOrderSingleEvents) {
  const auto compiled = compile(rule());
  State state;
  std::set<std::string> identifiers;
  for (std::uint64_t index = 20; index > 0; --index) {
    const auto input = event(1000 + static_cast<std::int64_t>(index), index);
    const auto result = advance(compiled, state, &input, {}, kTenant, 1100);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->decisions.size(), 1);
    EXPECT_TRUE(identifiers.insert(result->decisions.front().id).second);
  }
  EXPECT_EQ(identifiers.size(), 20);
}

TEST(LiveCorrelation, NonzeroSuppressionKeepsTheLastEmittedTimeAsItsBoundary) {
  auto spec = rule();
  spec.mutable_suppression()->set_seconds(1);
  const auto compiled = compile(spec);
  State state;
  const auto first = event(5000, 3);
  ASSERT_EQ(advance(compiled, state, &first, {}, kTenant, 5100)->decisions.size(), 1);
  const auto earlier = event(1000, 1);
  const auto delayed = advance(compiled, state, &earlier, {}, kTenant, 5100);
  ASSERT_TRUE(delayed);
  EXPECT_TRUE(delayed->decisions.empty());
  const auto inside = event(5500, 4);
  EXPECT_TRUE(advance(compiled, state, &inside, {}, kTenant, 5600)->decisions.empty());
  const auto boundary = event(6000, 5);
  EXPECT_EQ(advance(compiled, state, &boundary, {}, kTenant, 6100)->decisions.size(), 1);
}

TEST(LiveCorrelation, ArrivalTimeKeepsAQueuedSourceActiveAndStateRetainsOnlyRequiredAttributes) {
  auto spec = rule();
  spec.mutable_threshold()->set_count(2);
  spec.mutable_output()->set_summary_template("${event.attributes['x.required']}");
  const auto compiled = compile(spec);
  State state;
  auto input = event(1000);
  (*input.mutable_event()->mutable_attributes())["x.required"] = "Retained";
  (*input.mutable_event()->mutable_attributes())["x.unused"] = std::string(16000, 'x');
  Options options;
  options.idle_ms = 100;
  auto first = advance(compiled, state, &input, {}, kTenant, 50000, false, options);
  ASSERT_TRUE(first);
  EXPECT_FALSE(first->late);
  ASSERT_EQ(state.buffered_size(), 1);
  EXPECT_EQ(state.buffered(0).event().attributes().size(), 1);
  EXPECT_EQ(state.sources(0).received_ms(), 50000);
  EXPECT_EQ(state.watermark_ms(), 0);
  auto second = event(1100, 2);
  (*second.mutable_event()->mutable_attributes())["x.required"] = "Retained";
  auto next = advance(compiled, state, &second, {}, kTenant, 50001, false, options);
  ASSERT_TRUE(next);
  EXPECT_FALSE(next->late);
  auto closed = advance(compiled, state, nullptr, {}, kTenant, 51000, false, options);
  ASSERT_TRUE(closed);
  ASSERT_EQ(closed->decisions.size(), 1);
  EXPECT_EQ(closed->decisions.front().signal.summary(), "Retained");
}

TEST(LiveCorrelation, MaintenanceDoesNotStartSuppressionAndBoundaryRestoresSignals) {
  auto spec = rule();
  spec.mutable_suppression()->set_seconds(10);
  const auto compiled = compile(spec);
  rules::Context context;
  catalog::v1::MaintenanceWindow policy;
  policy.set_maintenance_id(kRule);
  policy.set_site_id(kSite);
  policy.set_zone_id(kZone);
  policy.set_version(3);
  *policy.mutable_starts_at() = Time::MillisecondsToTimestamp(1000);
  *policy.mutable_activated_at() = Time::MillisecondsToTimestamp(1000);
  *policy.mutable_updated_at() = Time::MillisecondsToTimestamp(1000);
  *policy.mutable_ends_at() = Time::MillisecondsToTimestamp(1500);
  policy.mutable_suppression()->add_rule_ids(kRule);
  context.maintenance.push_back(policy);
  State state;
  const auto first = event(1000);
  const auto suppressed = advance(compiled, state, &first, context, kTenant, 1100);
  ASSERT_TRUE(suppressed);
  ASSERT_EQ(suppressed->decisions.size(), 1);
  EXPECT_TRUE(suppressed->decisions.front().suppressed);
  EXPECT_FALSE(state.has_last_signal_ms());
  const auto after = event(1400, 2);
  const auto emitted = advance(compiled, state, &after, context, kTenant, 1500);
  ASSERT_TRUE(emitted);
  ASSERT_EQ(emitted->decisions.size(), 1);
  EXPECT_FALSE(emitted->decisions.front().suppressed);
}

TEST(LiveCorrelation, LateInputDoesNotReopenClosedWindowsAndFutureTimeIsCorrected) {
  const auto compiled = compile(rule());
  State state;
  ASSERT_TRUE(advance(compiled, state, nullptr, {}, kTenant, 50000));
  const auto old = event(1000);
  auto late = advance(compiled, state, &old, {}, kTenant, 50000);
  ASSERT_TRUE(late);
  EXPECT_TRUE(late->late);
  EXPECT_TRUE(late->decisions.empty());
  auto future = event(1000000);
  *future.mutable_envelope()->mutable_received_at() = Time::MillisecondsToTimestamp(50000);
  auto corrected = advance(compiled, state, &future, {}, kTenant, 50000);
  ASSERT_TRUE(corrected);
  EXPECT_TRUE(corrected->corrected);
  ASSERT_EQ(corrected->decisions.size(), 1);
  EXPECT_EQ(Time::TimestampToMilliseconds(corrected->decisions.front().signal.last_event_at()), 50000);
  State empty;
  const auto stale = advance(compiled, empty, &old, {}, kTenant, 4000000);
  ASSERT_TRUE(stale);
  EXPECT_TRUE(stale->late);
  EXPECT_TRUE(stale->decisions.empty());
}

TEST(LiveCorrelation, LateEvidenceStillContributesToAnOpenWindow) {
  auto spec = rule();
  spec.mutable_allowed_lateness()->set_seconds(0);
  spec.mutable_threshold()->set_count(2);
  const auto compiled = compile(spec);
  State state;
  const auto first = event(1000);
  ASSERT_TRUE(advance(compiled, state, &first, {}, kTenant, 1100));
  ASSERT_EQ(state.candidates_size(), 1);
  state.set_watermark_ms(1500);
  const auto second = event(1100, 2);
  const auto matched = advance(compiled, state, &second, {}, kTenant, 1600);
  ASSERT_TRUE(matched);
  EXPECT_TRUE(matched->late);
  ASSERT_EQ(matched->decisions.size(), 1);
  EXPECT_EQ(matched->decisions.front().signal.event_ids_size(), 2);
}

TEST(LiveCorrelation, ImmediateRulesKeepIndependentWindowsUnderReorderedDelivery) {
  for (const bool sequence : {false, true}) {
    auto spec = rule();
    auto* first = sequence ? spec.mutable_sequence()->add_steps() : spec.mutable_conjunction()->add_parts();
    first->add_types("intrusion.zone.alarm");
    auto* second = sequence ? spec.mutable_sequence()->add_steps() : spec.mutable_conjunction()->add_parts();
    second->add_types("intrusion.zone.restore");
    const auto compiled = compile(spec);
    State state;
    const auto future = event(5000, 3);
    ASSERT_TRUE(advance(compiled, state, &future, {}, kTenant, 5100));
    const auto early_restore = event(1100, 2, "intrusion.zone.restore");
    const auto mismatched = advance(compiled, state, &early_restore, {}, kTenant, 5100);
    ASSERT_TRUE(mismatched);
    EXPECT_TRUE(mismatched->decisions.empty());
    const auto early_alarm = event(1000, 1);
    const auto matched = advance(compiled, state, &early_alarm, {}, kTenant, 5100);
    ASSERT_TRUE(matched);
    ASSERT_EQ(matched->decisions.size(), 1);
    EXPECT_EQ(Time::TimestampToMilliseconds(matched->decisions.front().signal.first_event_at()), 1000);
    EXPECT_EQ(Time::TimestampToMilliseconds(matched->decisions.front().signal.last_event_at()), 1100);
    ASSERT_EQ(state.candidates_size(), 1);
    EXPECT_EQ(state.candidates(0).event().envelope().message_id(), future.envelope().message_id());
  }
}

TEST(LiveCorrelation, ImmediateMatchKeepsDeliveredEvidenceAndRestoresTheSameIdentifier) {
  auto spec = rule();
  spec.mutable_conjunction()->add_parts()->add_types("intrusion.zone.alarm");
  spec.mutable_conjunction()->add_parts()->add_types("intrusion.zone.restore");
  const auto compiled = compile(spec);
  const auto first = event(1000, 1);
  const auto alternative = event(1100, 2);
  const auto completion = event(1200, 3, "intrusion.zone.restore");
  State state;
  ASSERT_TRUE(advance(compiled, state, &alternative, {}, kTenant, 1300));
  State restored;
  ASSERT_TRUE(restored.ParseFromString(state.SerializeAsString()));
  const auto live = advance(compiled, state, &completion, {}, kTenant, 1300);
  const auto recovered = advance(compiled, restored, &completion, {}, kTenant, 1300);
  ASSERT_TRUE(live);
  ASSERT_TRUE(recovered);
  ASSERT_EQ(live->decisions.size(), 1);
  ASSERT_EQ(recovered->decisions.size(), 1);
  EXPECT_EQ(live->decisions.front().id, recovered->decisions.front().id);
  const auto late = advance(compiled, state, &first, {}, kTenant, 1300);
  ASSERT_TRUE(late);
  EXPECT_TRUE(late->decisions.empty());
  State other_order;
  ASSERT_TRUE(advance(compiled, other_order, &first, {}, kTenant, 1300));
  ASSERT_TRUE(advance(compiled, other_order, &alternative, {}, kTenant, 1300));
  const auto other = advance(compiled, other_order, &completion, {}, kTenant, 1300);
  ASSERT_TRUE(other);
  ASSERT_EQ(other->decisions.size(), 1);
  EXPECT_NE(live->decisions.front().id, other->decisions.front().id);
}

TEST(LiveCorrelation, IdleSourceDoesNotBlockAbsenceAfterRecovery) {
  auto spec = rule();
  spec.mutable_absence()->mutable_trigger()->add_types("intrusion.zone.alarm");
  spec.mutable_absence()->mutable_expected()->add_types("intrusion.zone.restore");
  const auto compiled = compile(spec);
  State state;
  const auto trigger = event(1000);
  ASSERT_TRUE(advance(compiled, state, &trigger, {}, kTenant, 1100));
  State restored;
  ASSERT_TRUE(restored.ParseFromString(state.SerializeAsString()));
  auto result = advance(compiled, restored, nullptr, {}, kTenant, 50000);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->decisions.size(), 1);
  EXPECT_EQ(Time::TimestampToMilliseconds(result->decisions.front().signal.absence_deadline()), 2000);
  EXPECT_TRUE(advance(compiled, restored, nullptr, {}, kTenant, 50001)->decisions.empty());
}

TEST(LiveCorrelation, PeriodicAbsenceUsesInventoryAndHalfOpenBoundary) {
  auto spec = rule();
  spec.mutable_absence()->mutable_expected()->add_types("intrusion.zone.alarm");
  const auto compiled = compile(spec);
  State state;
  const auto subject = event(1000);
  Options options;
  options.lateness_ms = 0;
  ASSERT_TRUE(advance(compiled, state, &subject, {}, kTenant, 1000, true, options));
  auto due = advance(compiled, state, nullptr, {}, kTenant, 2000, false, options);
  ASSERT_TRUE(due);
  ASSERT_EQ(due->decisions.size(), 1);
  EXPECT_EQ(Time::TimestampToMilliseconds(due->decisions.front().signal.absence_deadline()), 2000);
  EXPECT_TRUE(due->decisions.front().signal.event_ids().empty());
  auto again = advance(compiled, state, &subject, {}, kTenant, 2000, true, options);
  ASSERT_TRUE(again);
  EXPECT_TRUE(again->decisions.empty());
}

TEST(LiveCorrelation, EvaluationErrorAndQuotaLeaveOriginalStateIntact) {
  auto spec = rule();
  spec.mutable_output()->set_summary_template("${event.attributes['missing']}");
  const auto compiled = compile(spec);
  State state;
  const auto before = state.SerializeAsString();
  const auto input = event(1000);
  EXPECT_FALSE(advance(compiled, state, &input, {}, kTenant, 1100));
  EXPECT_EQ(state.SerializeAsString(), before);
  Options options;
  options.max_state_bytes = 1;
  EXPECT_FALSE(advance(compile(rule()), state, &input, {}, kTenant, 1100, false, options));
  EXPECT_EQ(state.SerializeAsString(), before);
}

TEST(LiveControl, RuleSetIsValidatedAndAppliedAsAWhole) {
  Control model({"intrusion.zone.alarm", "intrusion.zone.restore"});
  config::v1::ConfigRecord value;
  value.mutable_envelope()->set_tenant_id(kTenant);
  auto* set = value.mutable_rule_set();
  set->set_rule_set_id(kRule);
  set->set_version(1);
  *set->mutable_activated_at() = Time::MillisecondsToTimestamp(1000);
  *set->add_rules() = rule();
  ASSERT_TRUE(model.config(value));
  EXPECT_TRUE(model.config(value));
  auto invalid = value;
  invalid.mutable_rule_set()->set_version(2);
  invalid.mutable_rule_set()->mutable_rules(0)->set_version(2);
  invalid.mutable_rule_set()->mutable_rules(0)->mutable_filter()->add_types("invalid.type");
  EXPECT_FALSE(model.config(invalid));
  EXPECT_EQ(model.sets().at(kTenant).version(), 1);
  auto conflict = value;
  conflict.mutable_rule_set()->mutable_rules(0)->set_name("Changed");
  EXPECT_FALSE(model.config(conflict));
  EXPECT_FALSE(model.compiled(kTenant, conflict.rule_set().rules(0)));
  value.mutable_rule_set()->set_version(2);
  value.mutable_rule_set()->clear_rules();
  EXPECT_TRUE(model.config(value));
  EXPECT_TRUE(model.sets().at(kTenant).rules().empty());
}

TEST(LiveControl, WideKeysJoinZonesAndTenantsRemainSeparate) {
  auto input = event(1000);
  const auto first = rules::correlation_key(input, {}, processing::v1::CORRELATION_SCOPE_SITE);
  input.mutable_event()->set_zone_id(kRule);
  EXPECT_EQ(first, rules::correlation_key(input, {}, processing::v1::CORRELATION_SCOPE_SITE));
  input.mutable_envelope()->set_tenant_id(kSite);
  EXPECT_NE(first, rules::correlation_key(input, {}, processing::v1::CORRELATION_SCOPE_SITE));
}
}  // namespace
}  // namespace psim::correlation::live
