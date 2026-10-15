#include "psim/domain/processing/alarm.hpp"

#include <gtest/gtest.h>

namespace psim::domain::processing {
TEST(AlarmRule, MapsAlarmAndRestoreAndRejectsUnknownCodes) {
  const auto alarm = classify("ALARM");
  const auto restore = classify("RESTORE");
  if (!alarm || !restore) {
    FAIL() << "known mapping missing";
    return;
  }
  EXPECT_EQ(alarm->type, "intrusion.zone.alarm");
  EXPECT_TRUE(alarm->alarm);
  EXPECT_EQ(restore->type, "intrusion.zone.restore");
  EXPECT_FALSE(restore->alarm);
  EXPECT_FALSE(classify("unknown"));
  EXPECT_TRUE(alarm_rule("intrusion.zone.alarm"));
  EXPECT_FALSE(alarm_rule("intrusion.zone.restore"));
}
}  // namespace psim::domain::processing
