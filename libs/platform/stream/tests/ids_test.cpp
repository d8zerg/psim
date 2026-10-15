#include <gtest/gtest.h>

#include <string>

#include "psim/platform/stream/runtime.hpp"

namespace psim::platform::stream {
TEST(StreamIds, V7IsMonotonicAndCanonical) {
  auto previous = uuid7();
  EXPECT_TRUE(uuid(previous));
  EXPECT_EQ(previous.at(14), '7');
  for (int i = 0; i < 5000; ++i) {
    auto next = uuid7();
    EXPECT_LT(previous, next);
    previous = next;
  }
  EXPECT_FALSE(uuid("not-an-id"));
  EXPECT_FALSE(uuid("00000000-0000-0000-0000-00000000000Z"));
}

TEST(StreamIds, NamedIdsSeparateComponentsAndHashKnownBytes) {
  constexpr auto kSpace = "c8b07c6e-a8aa-5eb0-9744-705b51f8c211";
  EXPECT_EQ(uuid5(kSpace, {"a", "bc"}), uuid5(kSpace, {"a", "bc"}));
  EXPECT_NE(uuid5(kSpace, {"a", "bc"}), uuid5(kSpace, {"ab", "c"}));
  EXPECT_TRUE(uuid(uuid5(kSpace, {std::string(256, 'x')})));
  EXPECT_EQ(sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
}  // namespace psim::platform::stream
