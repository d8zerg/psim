#include "psim/platform/connector/state.hpp"

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>
#include <psim/connector/v1/connector.pb.h>

namespace psim::platform::connector {
namespace {
namespace wire = ::psim::connector::v1;

TEST(ConnectorState, SnapshotCannotOverwriteNewerEventAndReplayCannotEstablishFreshness) {
  StateTracker tracker;
  tracker.connected("session-1");
  wire::ConnectorEvent event;
  event.set_source_ref("door-1");
  event.set_source_epoch(1);
  event.set_source_seq(11);
  event.set_live_observation(true);
  event.set_observation_session_id("session-1");
  *event.mutable_occurred_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
  ASSERT_TRUE(tracker.apply(event, {{"alarm", "true"}}));
  wire::StateObservation snapshot;
  snapshot.mutable_cursor()->set_source_ref("door-1");
  snapshot.mutable_cursor()->set_source_epoch(1);
  snapshot.mutable_cursor()->set_source_seq(10);
  *snapshot.mutable_observed_at() = event.occurred_at();
  snapshot.set_availability(wire::DEVICE_AVAILABILITY_ONLINE);
  (*snapshot.mutable_values())["alarm"] = "false";
  EXPECT_FALSE(tracker.apply(snapshot));
  EXPECT_EQ(tracker.get("door-1").observation.values().at("alarm"), "true");
  tracker.disconnected();
  EXPECT_EQ(tracker.get("door-1").quality, Quality::kStale);
  tracker.connected("session-2");
  EXPECT_EQ(tracker.get("door-1").quality, Quality::kUnknown);
  event.set_source_seq(12);
  EXPECT_FALSE(tracker.apply(event, {{"alarm", "false"}}));
  event.set_live_observation(false);
  event.set_observation_session_id("session-2");
  EXPECT_FALSE(tracker.apply(event, {{"alarm", "false"}}));
  snapshot.mutable_cursor()->set_source_seq(12);
  ASSERT_TRUE(tracker.apply(snapshot));
  EXPECT_EQ(tracker.get("door-1").quality, Quality::kActual);
  tracker.expire(snapshot.observed_at().seconds() + 61, 60);
  EXPECT_EQ(tracker.get("door-1").quality, Quality::kStale);
  EXPECT_EQ(tracker.get("missing").quality, Quality::kUnknown);
}

TEST(ConnectorState, UnknownAvailabilityAndDisconnectedSnapshotsRemainUnknown) {
  StateTracker tracker;
  wire::StateObservation snapshot;
  snapshot.mutable_cursor()->set_source_ref("door-1");
  *snapshot.mutable_observed_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
  EXPECT_FALSE(tracker.apply(snapshot));
  tracker.connected("session");
  EXPECT_TRUE(tracker.apply(snapshot));
  EXPECT_EQ(tracker.get("door-1").quality, Quality::kUnknown);
}

TEST(ConnectorState, OlderObservationAtSameBoundaryCannotReplaceNewerConfirmation) {
  StateTracker tracker;
  tracker.connected("session");
  wire::StateObservation observation;
  observation.mutable_cursor()->set_source_ref("door-1");
  observation.mutable_cursor()->set_source_epoch(1);
  observation.mutable_cursor()->set_source_seq(7);
  observation.mutable_observed_at()->set_seconds(100);
  observation.set_availability(wire::DEVICE_AVAILABILITY_ONLINE);
  ASSERT_TRUE(tracker.apply(observation));
  observation.mutable_observed_at()->set_seconds(99);
  EXPECT_FALSE(tracker.apply(observation));
  observation.mutable_cursor()->set_source_epoch(2);
  observation.mutable_cursor()->set_source_seq(1);
  EXPECT_TRUE(tracker.apply(observation));
  observation.mutable_observed_at()->set_nanos(-1);
  EXPECT_FALSE(tracker.apply(observation));
}

}  // namespace
}  // namespace psim::platform::connector
