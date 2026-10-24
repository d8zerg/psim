#include "psim/domain/incident/operational.hpp"

#include <gtest/gtest.h>

namespace d = psim::domain::incident;

TEST(OperationalAlarm, ReturnBeforeAcknowledgementPreservesUnacknowledgedAlarm) {
  auto active = d::alarm({}, "activate", true, "confirmed condition", 100);
  ASSERT_TRUE(active);
  auto returned = d::alarm(*active, "return", true, "condition cleared", 200);
  ASSERT_TRUE(returned);
  EXPECT_FALSE(returned->active);
  EXPECT_FALSE(returned->acknowledged);
  auto ack = d::alarm(*returned, "acknowledge", false, {}, 300);
  ASSERT_TRUE(ack);
  EXPECT_FALSE(ack->active);
  EXPECT_TRUE(ack->acknowledged);
  auto reactivated = d::alarm(*ack, "activate", true, "condition returned", 400);
  ASSERT_TRUE(reactivated);
  EXPECT_TRUE(reactivated->active);
  EXPECT_FALSE(reactivated->acknowledged);
}

TEST(OperationalAlarm, VisibilityControlsNeverClearConditionOrAcknowledge) {
  const d::Alarm state{.active = true};
  auto shelved = d::alarm(state, "shelve", false, "inspection", 1000, 2000);
  ASSERT_TRUE(shelved);
  auto suppressed = d::alarm(*shelved, "suppress", true, "policy", 1000, 3000);
  ASSERT_TRUE(suppressed);
  EXPECT_TRUE(suppressed->active);
  EXPECT_FALSE(suppressed->acknowledged);
  EXPECT_EQ(suppressed->shelved_until, 2000);
  EXPECT_EQ(suppressed->suppressed_until, 3000);
  EXPECT_FALSE(d::alarm(state, "suppress", false, "policy", 1000, 3000));
  EXPECT_FALSE(d::alarm(state, "shelve", false, "", 1000, 3000));
  EXPECT_FALSE(d::alarm(state, "shelve", false, "inspection", 1000, 1000));
  EXPECT_FALSE(d::alarm(state, "shelve", false, "inspection", 1000, 86401001));
  EXPECT_FALSE(d::alarm(state, "return", false, "clear", 1000));
  EXPECT_FALSE(d::alarm(state, "unknown", true, "reason", 1000));
}

TEST(OperationalSla, PinnedDeadlineDistinguishesUnknownRiskAndBreach) {
  EXPECT_EQ(d::sla_risk(100, 0, 10), "unknown");
  EXPECT_EQ(d::sla_risk(5000, 10000, 1000), "on-time");
  EXPECT_EQ(d::sla_risk(8200, 10000, 1000), "at-risk");
  EXPECT_EQ(d::sla_risk(10000, 10000, 1000), "breached");
}
