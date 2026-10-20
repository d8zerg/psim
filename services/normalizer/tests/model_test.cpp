#include "model.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "condition.hpp"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/internal.pb.h"

namespace psim::normalizer {
namespace {
constexpr const char* kTenant = "11111111-1111-4111-8111-111111111111";
constexpr const char* kConnector = "22222222-2222-4222-8222-222222222222";
constexpr const char* kSite = "33333333-3333-4333-8333-333333333333";
constexpr const char* kZone = "44444444-4444-4444-8444-444444444444";
constexpr const char* kDevice = "55555555-5555-4555-8555-555555555555";
constexpr const char* kSource = "66666666-6666-4666-8666-666666666666";
constexpr const char* kType = "77777777-7777-4777-8777-777777777777";

Prepared& require_prepared(p::Result<std::optional<Prepared>>& value) {
  if (!value || !value->has_value()) {
    throw std::logic_error("expected a normalized event");
  }
  return value->value();
}

class Normalizer : public ::testing::Test {
 public:
  Model model{{{"generic.alarm", {common::v1::EVENT_CLASS_ALARM, common::v1::SEVERITY_HIGH}},
               {"generic.restore", {common::v1::EVENT_CLASS_RESTORE, common::v1::SEVERITY_INFO}}}};

  void SetUp() override {
    catalog::v1::CatalogRecord record;
    record.mutable_envelope()->set_tenant_id(kTenant);
    auto* connector = record.mutable_connector();
    connector->set_connector_id(kConnector);
    connector->set_connector_type("test");
    connector->set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
    connector->set_version(1);
    ASSERT_TRUE(model.catalog(record));
    record.clear_connector();
    auto* type = record.mutable_device_type();
    type->set_device_type_id(kType);
    type->set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
    type->set_version(1);
    ASSERT_TRUE(model.catalog(record));
    record.clear_device_type();
    auto* zone = record.mutable_location();
    zone->set_location_id(kZone);
    zone->set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
    zone->set_version(1);
    ASSERT_TRUE(model.catalog(record));
    record.clear_location();
    auto* device = record.mutable_device();
    device->set_device_id(kDevice);
    device->set_connector_id(kConnector);
    device->set_device_type_id(kType);
    device->set_site_id(kSite);
    device->set_zone_id(kZone);
    device->set_version(1);
    device->set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
    auto* source = device->add_sources();
    source->set_source_id(kSource);
    source->set_external_ref("door");
    ASSERT_TRUE(model.catalog(record));
    config::v1::ConfigRecord config;
    config.mutable_envelope()->set_tenant_id(kTenant);
    auto* mapping = config.mutable_mapping_rule_set();
    mapping->set_connector_type("test");
    mapping->set_version(1);
    mapping->set_taxonomy_version("1");
    auto* rule = mapping->add_rules();
    rule->set_raw_code("ALARM");
    rule->set_type("generic.alarm");
    (*rule->mutable_attribute_map())["loop"] = "loop";
    rule = mapping->add_rules();
    rule->set_raw_code("REST*");
    rule->set_type("generic.restore");
    ASSERT_TRUE(model.config(config));
  }

  static ingest::v1::RawEventRecord raw(std::uint64_t sequence = 1, std::int64_t seconds = 1000) {
    ingest::v1::RawEventRecord record;
    auto* envelope = record.mutable_envelope();
    envelope->set_tenant_id(kTenant);
    envelope->set_message_id(p::stream::uuid7());
    envelope->set_sequence(sequence);
    envelope->mutable_occurred_at()->set_seconds(seconds);
    envelope->mutable_received_at()->set_seconds(1000);
    auto* event = record.mutable_raw_event();
    event->set_connector_id(kConnector);
    event->set_source_id(kSource);
    event->set_source_ref("door");
    event->set_raw_code("ALARM");
    event->set_live_observation(true);
    event->set_observation_session_id("session");
    event->set_gateway_session_id("session");
    (*event->mutable_attributes())["loop"] = "1";
    (*event->mutable_attributes())["vendor"] = "value";
    return record;
  }
};

TEST_F(Normalizer, MapsCatalogTaxonomyAttributesAndFreshness) {
  auto result = model.prepare(raw());
  ASSERT_TRUE(result);
  ASSERT_TRUE(*result);
  const auto& event = require_prepared(result).event.event();
  EXPECT_EQ(event.site_id(), kSite);
  EXPECT_EQ(event.zone_id(), kZone);
  EXPECT_EQ(event.device_type_id(), kType);
  EXPECT_EQ(event.type(), "generic.alarm");
  EXPECT_TRUE(event.significant());
  EXPECT_EQ(event.attributes().at("loop"), "1");
  EXPECT_EQ(event.attributes().at("x.vendor"), "value");
  EXPECT_TRUE(event.live_observation());
  EXPECT_EQ(event.state_observed_at().seconds(), 1000);
}

TEST_F(Normalizer, DeduplicatesPendingAndCommittedAndRollsBack) {
  const auto input = raw();
  auto result = model.prepare(input);
  ASSERT_TRUE(result);
  ASSERT_TRUE(*result);
  model.stage(std::move(require_prepared(result)), 0);
  EXPECT_FALSE(*model.prepare(input));
  model.abort();
  EXPECT_TRUE(*model.prepare(input));
  result = model.prepare(input);
  model.stage(std::move(require_prepared(result)), 0);
  model.commit();
  EXPECT_FALSE(*model.prepare(input));
}

TEST_F(Normalizer, ReordersSequencesAndRestoresEpochsWithoutDuplicates) {
  for (const auto sequence : {100U, 1U, 99U, 101U, 50U}) {
    auto result = model.prepare(raw(sequence));
    ASSERT_TRUE(result);
    ASSERT_TRUE(*result);
    const auto state = require_prepared(result).state;
    model.stage(std::move(require_prepared(result)), 3);
    model.commit();
    ASSERT_TRUE(model.restore(state, 3));
    EXPECT_FALSE(*model.prepare(raw(sequence)));
  }
  auto next_epoch = raw(1);
  next_epoch.mutable_raw_event()->set_source_epoch(1);
  EXPECT_TRUE(*model.prepare(next_epoch));
  model.revoke(3);
  EXPECT_TRUE(*model.prepare(raw(100)));
}

TEST_F(Normalizer, OldAndOtherSessionEventsNeverAdvanceConfirmation) {
  auto first = model.prepare(raw());
  model.stage(std::move(require_prepared(first)), 0);
  model.commit();
  auto older = model.prepare(raw(2, 999));
  ASSERT_TRUE(older);
  EXPECT_FALSE(require_prepared(older).event.event().live_observation());
  EXPECT_FALSE(require_prepared(older).event.event().has_state_observed_at());
  EXPECT_EQ(require_prepared(older).state.source_state().last_confirmed_at().seconds(), 1000);
  auto historical = raw(3, 1001);
  historical.mutable_raw_event()->set_observation_session_id("old");
  auto result = model.prepare(historical);
  ASSERT_TRUE(result);
  EXPECT_FALSE(require_prepared(result).event.event().live_observation());
}

TEST_F(Normalizer, ValidatesCatalogMappingAndRawMetadata) {
  auto input = raw();
  input.mutable_raw_event()->set_source_id(kDevice);
  EXPECT_FALSE(model.prepare(input));
  input = raw();
  input.mutable_envelope()->set_tenant_id(kDevice);
  EXPECT_FALSE(model.prepare(input));
  input = raw();
  input.mutable_envelope()->set_sequence(0);
  EXPECT_FALSE(model.prepare(input));
  input = raw();
  input.mutable_raw_event()->set_raw_code("UNKNOWN");
  EXPECT_FALSE(model.prepare(input));
  input = raw();
  input.mutable_raw_event()->set_raw_code("RESTORE");
  auto result = model.prepare(input);
  ASSERT_TRUE(result);
  EXPECT_EQ(require_prepared(result).event.event().type(), "generic.restore");
  EXPECT_FALSE(require_prepared(result).event.event().significant());
}

TEST_F(Normalizer, RejectsOutsideWindowAndCorruptState) {
  auto first = model.prepare(raw(5000));
  model.stage(std::move(require_prepared(first)), 0);
  model.commit();
  EXPECT_FALSE(model.prepare(raw(1)));
  const processing::v1::NormalizerStateRecord state;
  EXPECT_FALSE(model.restore(state, 0));
}

TEST_F(Normalizer, IgnoresOldCatalogVersionsAndDisablesOnlyCurrentVersion) {
  catalog::v1::CatalogRecord record;
  record.mutable_envelope()->set_tenant_id(kTenant);
  auto* connector = record.mutable_connector();
  connector->set_connector_id(kConnector);
  connector->set_connector_type("test");
  connector->set_state(catalog::v1::RESOURCE_STATE_DISABLED);
  connector->set_version(2);
  ASSERT_TRUE(model.catalog(record));
  EXPECT_FALSE(model.prepare(raw()));
  connector->set_version(1);
  connector->set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
  ASSERT_TRUE(model.catalog(record));
  EXPECT_FALSE(model.prepare(raw()));
  connector->set_version(3);
  ASSERT_TRUE(model.catalog(record));
  EXPECT_TRUE(model.prepare(raw()));
}

TEST_F(Normalizer, RejectsInvalidMappingWithoutReplacingLastGoodVersion) {
  config::v1::ConfigRecord config;
  config.mutable_envelope()->set_tenant_id(kTenant);
  auto* mapping = config.mutable_mapping_rule_set();
  mapping->set_connector_type("test");
  mapping->set_version(2);
  mapping->set_taxonomy_version("1");
  auto* rule = mapping->add_rules();
  rule->set_raw_code("ALARM");
  rule->set_type("absent.type");
  EXPECT_FALSE(model.config(config));
  EXPECT_TRUE(model.prepare(raw()));
  rule->set_type("generic.restore");
  rule->set_condition(R"(raw.attributes["loop"] == "1")");
  ASSERT_TRUE(model.config(config));
  auto result = model.prepare(raw());
  ASSERT_TRUE(result);
  EXPECT_EQ(require_prepared(result).event.event().type(), "generic.restore");
}

TEST_F(Normalizer, RestoredConfirmationAndAbortedObservationNeverMoveFreshnessBackwards) {
  auto first = model.prepare(raw());
  const auto state = require_prepared(first).state;
  ASSERT_TRUE(model.restore(state, 0));
  auto next = raw(2, 1001);
  auto prepared = model.prepare(next);
  ASSERT_TRUE(prepared);
  ASSERT_TRUE(*prepared);
  model.stage(std::move(require_prepared(prepared)), 0);
  model.abort();
  prepared = model.prepare(next);
  ASSERT_TRUE(prepared);
  EXPECT_TRUE(require_prepared(prepared).event.event().live_observation());
  auto older_epoch = raw(1, 999);
  older_epoch.mutable_raw_event()->set_source_epoch(10);
  auto old = model.prepare(older_epoch);
  ASSERT_TRUE(old);
  EXPECT_FALSE(require_prepared(old).event.event().live_observation());
}

TEST(NormalizerCondition, EvaluatesTypedBoundedStringAndBooleanExpressions) {
  ingest::v1::RawEvent raw;
  raw.set_raw_code("ALARM");
  raw.set_source_ref("door-1");
  (*raw.mutable_attributes())["loop"] = "1";
  for (const auto* expression : {"", "true", "!false", R"(raw.raw_code == "ALARM")",
                                 R"(raw.raw_code.startsWith("AL") && raw.source_ref.endsWith("1"))",
                                 R"(has(raw.attributes["loop"]) && raw.attributes["loop"] != "2")",
                                 R"((false || true) && raw.raw_code.contains("AR"))"}) {
    auto result = condition(expression, raw);
    ASSERT_TRUE(result) << expression;
    EXPECT_TRUE(*result) << expression;
  }
  for (const auto* expression : {"false", R"(has(raw.attributes["missing"]))",
                                 R"(raw.attributes["missing"] != "value")", R"(raw.raw_code == "RESTORE")"}) {
    auto result = condition(expression, raw);
    ASSERT_TRUE(result) << expression;
    EXPECT_FALSE(*result) << expression;
  }
  for (const auto* expression : {"raw.unknown == 1", "raw.raw_code + 1", R"(true == "true")",
                                 R"(raw.raw_code.matches(".*"))", "(true", R"("unterminated)", "has(true)"}) {
    EXPECT_FALSE(condition(expression, raw)) << expression;
  }
  EXPECT_FALSE(condition(std::string(100, '!') + "true", raw));
  EXPECT_FALSE(condition(std::string(4001, ' '), raw));
}

TEST_F(Normalizer, CorrectsFutureEventTimeAndPreservesOriginalTimestamp) {
  auto value = model.prepare(raw(1, 1006));
  ASSERT_TRUE(value);
  const auto& event = require_prepared(value).event;
  EXPECT_TRUE(event.event().clock_skew_corrected());
  EXPECT_EQ(event.envelope().occurred_at().seconds(), 1000);
  EXPECT_TRUE(event.event().attributes().contains("x.source_occurred_at"));
}

TEST_F(Normalizer, CoalescedCursorRetainsEverySequenceAndRollsBackAsOneTransaction) {
  for (std::uint64_t sequence = 1; sequence <= 1000; ++sequence) {
    auto value = model.prepare(raw(sequence));
    model.stage(std::move(require_prepared(value)), 0);
  }
  const auto records = model.pending_records();
  ASSERT_EQ(records.size(), 1);
  EXPECT_EQ(records.front().second.source_state().max_sequence(), 1000);
  model.abort();
  EXPECT_TRUE(model.prepare(raw(1))->has_value());
  ASSERT_TRUE(model.restore(records.front().second, 0));
  for (std::uint64_t sequence = 1; sequence <= 1000; ++sequence) {
    auto value = model.prepare(raw(sequence));
    ASSERT_TRUE(value);
    EXPECT_FALSE(value->has_value());
  }
}

TEST(NormalizerTaxonomy, LoadsRepositoryTaxonomyAndRejectsMissingFile) {
  EXPECT_FALSE(load_taxonomy("/missing/taxonomy.yaml"));
  auto taxonomy = load_taxonomy("/repo/contracts/taxonomy/v1/taxonomy.yaml");
  ASSERT_TRUE(taxonomy);
  EXPECT_EQ(taxonomy->at("fire.detector.alarm").severity, common::v1::SEVERITY_CRITICAL);
}
}  // namespace
}  // namespace psim::normalizer
