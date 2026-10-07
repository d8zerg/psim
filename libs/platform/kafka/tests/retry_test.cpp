#include <gtest/gtest.h>

#include <chrono>

#include "psim/platform/kafka/processor.hpp"

namespace psim::platform::kafka {
namespace {

using std::chrono::milliseconds;

TEST(RetryPolicy, GrowsExponentiallyUpToTheLimit) {
  const RetryPolicy policy{.initial = milliseconds(100), .max = milliseconds(1000), .multiplier = 2.0};
  EXPECT_EQ(policy.delay(1), milliseconds(100));
  EXPECT_EQ(policy.delay(2), milliseconds(200));
  EXPECT_EQ(policy.delay(4), milliseconds(800));
  EXPECT_EQ(policy.delay(5), milliseconds(1000));
  EXPECT_EQ(policy.delay(60), milliseconds(1000));
  EXPECT_EQ(policy.delay(0), milliseconds(100));
}

}  // namespace
}  // namespace psim::platform::kafka
