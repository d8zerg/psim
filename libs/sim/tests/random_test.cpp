#include "psim/sim/random.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <tuple>

namespace {

TEST(SimRandom, MatchesSplitMix64KnownVector) {
  psim::sim::Random random(0);
  EXPECT_EQ(random.next(), 0xe220a8397b1dcdafULL);
  EXPECT_EQ(random.next(), 0x6e789e6aa1b965f4ULL);
  EXPECT_EQ(random.next(), 0x06c45d188009454fULL);
}

TEST(SimRandom, ReproducesBoundedDecisionsAndRejectsZero) {
  psim::sim::Random first(42);
  psim::sim::Random second(42);
  for (const auto bound : std::array<std::uint64_t, 4>{1, 3, 100, 0x8000000000000001ULL}) {
    for (int sample = 0; sample < 100; ++sample) {
      const auto value = first.bounded(bound);
      EXPECT_LT(value, bound);
      EXPECT_EQ(value, second.bounded(bound));
    }
  }
  EXPECT_THROW(std::ignore = first.bounded(0), std::invalid_argument);
}

}  // namespace
