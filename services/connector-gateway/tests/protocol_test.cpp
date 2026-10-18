#include "protocol.hpp"

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/connector/v1/connector.pb.h>

#include <chrono>
#include <string>

#include "psim/platform/stream/runtime.hpp"

namespace psim::connector_gateway {
namespace {
using Util = google::protobuf::util::TimeUtil;

TEST(GatewayProtocol, NegotiatesOnlyKnownVersionsAndCapabilities) {
  connector::v1::Register registration;
  registration.add_protocol_versions("2.0");
  EXPECT_TRUE(protocol(registration).empty());
  registration.add_protocol_versions("1.0");
  EXPECT_EQ(protocol(registration), "1.0");
  registration.add_protocol_versions("1.1");
  EXPECT_EQ(protocol(registration), "1.1");
  EXPECT_FALSE(supports(registration, "snapshot.v1"));
  registration.add_capabilities("state.v1");
  EXPECT_TRUE(supports(registration, "state.v1"));
}

TEST(GatewayProtocol, RegistrationBindsIdentityFingerprintStateAndExpiry) {
  const auto now = std::chrono::system_clock::now();
  Registration registration{.tenant = platform::stream::uuid7()};
  auto& value = registration.connector;
  value.set_connector_id(platform::stream::uuid7());
  value.set_state(catalog::v1::RESOURCE_STATE_ACTIVE);
  Certificate certificate{"psim://connector/" + value.connector_id(), std::string(64, 'a'),
                          now + std::chrono::hours(1)};
  auto* fingerprint = value.add_certificates();
  fingerprint->set_sha256(certificate.fingerprint);
  *fingerprint->mutable_not_after() = Util::SecondsToTimestamp(Util::GetCurrentTime().seconds() + 3600);
  EXPECT_TRUE(authorized(registration, certificate, now));
  auto other = certificate;
  other.identity = "psim://connector/" + platform::stream::uuid7();
  EXPECT_FALSE(authorized(registration, other, now));
  other = certificate;
  other.fingerprint = std::string(64, 'b');
  EXPECT_FALSE(authorized(registration, other, now));
  other = certificate;
  other.expires_at = now;
  EXPECT_FALSE(authorized(registration, other, now));
  value.set_state(catalog::v1::RESOURCE_STATE_DISABLED);
  EXPECT_FALSE(authorized(registration, certificate, now));
}

connector::v1::ConnectorEvent event() {
  connector::v1::ConnectorEvent value;
  value.set_event_id(platform::stream::uuid7());
  value.set_source_ref("door/alarm");
  value.set_source_seq(1);
  value.set_raw_code("ALARM");
  *value.mutable_occurred_at() = Util::GetCurrentTime();
  return value;
}

TEST(GatewayProtocol, ValidatesBoundsWithoutRejectingBufferedHistory) {
  auto input = event();
  EXPECT_TRUE(validate(input));
  input.mutable_occurred_at()->set_seconds(input.occurred_at().seconds() - 86400);
  EXPECT_TRUE(validate(input));
  input.set_source_seq(0);
  EXPECT_FALSE(validate(input));
  input.set_source_seq(1);
  input.set_event_id("22222222-2222-4222-8222-222222222222");
  EXPECT_FALSE(validate(input));
  input = event();
  (*input.mutable_attributes())["oversized"] = std::string(4097, 'x');
  EXPECT_FALSE(validate(input));
  input = event();
  input.mutable_occurred_at()->set_nanos(1000000000);
  EXPECT_FALSE(validate(input));
}

TEST(GatewayProtocol, ReplayAndForeignSessionsCannotEstablishFreshness) {
  auto input = event();
  const auto session = platform::stream::uuid7();
  const auto now = std::chrono::system_clock::now();
  input.set_observation_session_id(session);
  EXPECT_FALSE(live(input, session, now));
  input.set_live_observation(true);
  EXPECT_TRUE(live(input, session, now));
  EXPECT_FALSE(live(input, "another-session", now));
  input.mutable_occurred_at()->set_seconds(input.occurred_at().seconds() - 61);
  EXPECT_FALSE(live(input, session, now));
  *input.mutable_occurred_at() = Util::SecondsToTimestamp(Util::GetCurrentTime().seconds() + 10);
  EXPECT_FALSE(live(input, session, now));
}

TEST(GatewayProtocol, RejectsAmbiguousSnapshotsAndInvalidObservations) {
  connector::v1::StateReport report;
  report.set_snapshot(true);
  EXPECT_FALSE(validate(report));
  report.set_snapshot_id(platform::stream::uuid7());
  EXPECT_TRUE(validate(report));
  auto* observation = report.add_observations();
  observation->mutable_cursor()->set_source_ref("door/alarm");
  *observation->mutable_observed_at() = Util::GetCurrentTime();
  observation->set_availability(connector::v1::DEVICE_AVAILABILITY_ONLINE);
  EXPECT_TRUE(validate(report));
  *report.add_observations() = *observation;
  EXPECT_FALSE(validate(report));
  report.mutable_observations()->RemoveLast();
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): reject unknown values received over protobuf
  report.mutable_observations(0)->set_availability(static_cast<connector::v1::DeviceAvailability>(999));
  EXPECT_FALSE(validate(report));
}
}  // namespace
}  // namespace psim::connector_gateway
