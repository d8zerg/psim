#include "events.hpp"

#include <clickhouse/columns/date.h>
#include <gtest/gtest.h>

#include <array>

#include "psim/common/v1/types.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/processing/v1/event.pb.h"

namespace psim::event_history {
namespace {
processing::v1::EventRecord event(bool significant = false) {
  processing::v1::EventRecord record;
  auto* envelope = record.mutable_envelope();
  envelope->set_tenant_id("11111111-1111-4111-8111-111111111111");
  envelope->set_message_id("22222222-2222-4222-8222-222222222222");
  envelope->mutable_occurred_at()->set_seconds(1000);
  auto* value = record.mutable_event();
  value->set_site_id(envelope->tenant_id());
  value->set_zone_id(envelope->tenant_id());
  value->set_device_id(envelope->tenant_id());
  value->set_type("generic.alarm");
  value->set_severity(common::v1::SEVERITY_HIGH);
  value->set_significant(significant);
  return record;
}

TEST(EventHistory, ConvertsEveryValidRowAndAccountsForRejectedRecords) {
  const Events events;
  const std::array records{event(), processing::v1::EventRecord{}, event(true)};
  auto result = events.convert_records(records);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->block.GetRowCount(), 2);
  ASSERT_EQ(result->rejected.size(), 1);
  EXPECT_EQ(result->rejected.front().index, 1);
  auto expires = result->block[13]->As<::clickhouse::ColumnDateTime64>();
  EXPECT_EQ(expires->At(0), (1000LL + 7LL * 86400) * 1000);
  EXPECT_EQ(expires->At(1), (1000LL + 90LL * 86400) * 1000);
}

TEST(EventHistory, AppliesVersionedTenantRetentionAndRejectsInvalidDurations) {
  Events events;
  config::v1::ConfigRecord config;
  config.mutable_envelope()->set_tenant_id(event().envelope().tenant_id());
  config.mutable_retention()->mutable_event_history_all()->set_seconds(60);
  config.mutable_retention()->mutable_event_history_significant()->set_seconds(120);
  config.mutable_retention()->set_version(2);
  ASSERT_TRUE(events.config(config));
  config.mutable_retention()->set_version(1);
  config.mutable_retention()->mutable_event_history_all()->set_seconds(10);
  ASSERT_TRUE(events.config(config));
  auto result = events.convert_records(std::array{event()});
  ASSERT_TRUE(result);
  EXPECT_EQ(result->block[13]->As<::clickhouse::ColumnDateTime64>()->At(0), 1060000);
  config.mutable_retention()->mutable_event_history_all()->set_seconds(-1);
  EXPECT_FALSE(events.config(config));
}

TEST(EventHistory, MigrationUsesReplicatedStorageAndValidatesCluster) {
  EXPECT_EQ(migration("psim").expand.size(), 1);
  EXPECT_TRUE(migration("unsafe;drop").expand.empty());
}
}  // namespace
}  // namespace psim::event_history
