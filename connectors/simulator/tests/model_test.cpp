#include "../model.hpp"

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>
#include <psim/connector/v1/connector.pb.h>

#include <stdexcept>
#include <stop_token>

namespace psim::connectors::simulator {
TEST(Simulator, ProfilesBoundDevicesAndProvideReconciliationSequence) {
  const Model model({.devices = 2000, .run_id = "load-1"});
  EXPECT_EQ(model.event(0).source_ref(), "device-1");
  EXPECT_EQ(model.event(2000).source_ref(), "device-1");
  EXPECT_EQ(model.event(1).raw_code(), "FIRE");
  EXPECT_EQ(model.event(2).raw_code(), "OFFLINE");
  EXPECT_EQ(model.event(2000).attributes().at("generator_seq"), "2001");
  EXPECT_EQ(model.snapshot({}).size(), 1000);
  EXPECT_THROW(Model(Profile{.devices = 0}), std::invalid_argument);
  EXPECT_THROW(Model(Profile{.scenario = "invalid"}), std::invalid_argument);
}

TEST(Simulator, BurstScheduleIsOpenLoopAndPreservesMeanRate) {
  const Model model({.rate = 1000, .burst_every = 100, .burst_size = 20, .scenario = "burst"});
  EXPECT_EQ(model.scheduled_nanoseconds(0), 0);
  EXPECT_EQ(model.scheduled_nanoseconds(19), 0);
  EXPECT_EQ(model.scheduled_nanoseconds(20), 20000000);
  EXPECT_EQ(model.scheduled_nanoseconds(100), 100000000);
  EXPECT_EQ(model.scheduled_nanoseconds(119), 100000000);
}

TEST(Simulator, CommandsChangeObservedStateAndRejectUnsupportedTargets) {
  Model model({.devices = 2});
  wire::Command command;
  command.set_device_external_id("device-1");
  command.set_capability("arm");
  *command.mutable_deadline_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
  command.mutable_deadline_at()->set_seconds(command.deadline_at().seconds() + 30);
  EXPECT_TRUE(model.command(command, {}).success());
  EXPECT_EQ(model.snapshot({})[0].values().at("armed"), "true");
  command.set_capability("disarm");
  EXPECT_TRUE(model.command(command, {}).success());
  EXPECT_EQ(model.snapshot({})[0].values().at("armed"), "false");
  command.set_capability("unsupported");
  EXPECT_EQ(model.command(command, {}).code(), "COMMAND_CAPABILITY_UNSUPPORTED");
  command.set_device_external_id("device-3");
  EXPECT_FALSE(model.command(command, {}).success());
  command.set_device_external_id("device-1");
  command.set_capability("arm");
  command.mutable_deadline_at()->set_seconds(1);
  EXPECT_EQ(model.command(command, {}).code(), "COMMAND_EXPIRED");
  const std::stop_source stop;
  stop.request_stop();
  EXPECT_TRUE(model.snapshot(stop.get_token()).empty());
}
}  // namespace psim::connectors::simulator
