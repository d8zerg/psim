#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "common.hpp"

namespace psim::ctl {
namespace {
TEST(Operations, RecoveryTimeValidatesUtcCalendarAndMicroseconds) {
  EXPECT_DOUBLE_EQ(recovery_time("2000-01-01T00:00:00Z"), 946684800);
  EXPECT_NEAR(recovery_time("2000-01-01T00:00:00.123456Z"), 946684800.123456, 0.000001);
  for (const auto* value : {"2001-02-29T00:00:00Z", "2000-01-01T24:00:00Z", "2000-01-01T00:00:00+03:00",
                            "2000-01-01T00:00:00Z'", "2000-01-01T00:00:00x123456Z"}) {
    EXPECT_THROW((void)recovery_time(value), OperationError);
  }
}

TEST(Operations, StatusHeartbeatPreservesCopyAgeAndFailureDoesNotEraseLastSuccess) {
  Temporary temporary;
  Context context;
  context.config = {{"backup", {{"status_file", temporary.path("backups.json").string()}}}};
  backup_status(context, {{"plans", {{"last_success_seconds", 100}, {"size_bytes", 42}, {"last_attempt_success", 1}}}});
  backup_status(context, {{"plans", {{"last_attempt_success", 0}}}});
  const auto document = read_json(temporary.path("backups.json"));
  EXPECT_EQ(document.at("backups").at("plans").at("last_success_seconds"), 100);
  EXPECT_EQ(document.at("backups").at("plans").at("last_attempt_success"), 0);
  EXPECT_EQ(document.at("backups").at("postgres").at("last_success_seconds"), 0);
}

TEST(Operations, OutputCannotOverwriteAnExistingDestinationOrFollowASymlink) {
  const Temporary temporary;
  const auto path = temporary.path("output");
  write_file(path, "original");
  EXPECT_THROW(write_file(path, "changed"), std::runtime_error);
  EXPECT_EQ(read_file(path), "original");
  const auto link = temporary.path("link");
  std::filesystem::create_symlink(path, link);
  EXPECT_THROW(read_file(link), std::runtime_error);
}

TEST(Operations, InputLimitsAndFileDigestAreEnforced) {
  const Temporary temporary;
  const auto path = temporary.path("input");
  write_file(path, "abc");
  EXPECT_THROW(read_file(path, 2), std::runtime_error);
  EXPECT_EQ(file_hash(path), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
}  // namespace
}  // namespace psim::ctl
