#include "psim/platform/build_info.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

TEST(BuildInfo, ReportsProjectVersionAndToolchain) {
  const auto info = psim::platform::build_info();
  EXPECT_FALSE(info.version.empty());
  EXPECT_GE(std::count(info.version.begin(), info.version.end(), '.'), 2);
  EXPECT_FALSE(info.git_commit.empty());
  EXPECT_TRUE(info.compiler.starts_with("Clang 21")) << info.compiler;
}

}  // namespace
