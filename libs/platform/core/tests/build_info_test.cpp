#include "psim/platform/build_info.hpp"

#include <gtest/gtest.h>

namespace {

TEST(BuildInfo, ReportsProjectVersionAndToolchain) {
  const auto info = psim::platform::build_info();
  EXPECT_EQ(info.version, "0.1.0");
  EXPECT_FALSE(info.git_commit.empty());
  EXPECT_TRUE(info.compiler.starts_with("Clang 21")) << info.compiler;
}

}  // namespace
