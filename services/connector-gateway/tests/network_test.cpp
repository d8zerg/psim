#include <google/rpc/error_details.pb.h>
#include <google/rpc/status.pb.h>
#include <grpcpp/support/status.h>
#include <gtest/gtest.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/connector/v1/administration.pb.h>
#include <psim/connector/v1/connector.pb.h>
#include <psim/ingest/v1/raw_event.pb.h>
#include <psim/ingest/v1/state.pb.h>
#include <psim/response/v1/command.pb.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/redis/connection.hpp"
#include "psim/platform/redis/serde.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "support.hpp"

namespace psim::connector_gateway::testing {
TEST(GatewayNetwork, RejectsUnregisteredTrustedCertificate) {
  Stream stream(19);
  EXPECT_FALSE(stream.connect());
  const auto audit = run(find<psim::audit::v1::AuditEventRecord>("psim.audit.v1", identity(19), "access.denied"));
  EXPECT_EQ(audit.audit_entry().outcome(), psim::audit::v1::AUDIT_OUTCOME_DENIED);
  EXPECT_EQ(audit.envelope().tenant_id(), kTenant);
}

TEST(GatewayNetwork, DurableBatchRejectionsAndHistoricalMetadata) {
  enroll(0);
  Stream stream(0);
  ASSERT_TRUE(stream.connect());
  auto historic = event();
  historic.mutable_occurred_at()->set_seconds(historic.occurred_at().seconds() - 86400);
  auto invalid = event(2);
  invalid.set_source_seq(0);
  auto ack = stream.batch({historic, invalid});
  EXPECT_EQ(ack.accepted(), 1);
  ASSERT_EQ(ack.rejections_size(), 1);
  EXPECT_EQ(ack.rejections(0).event_id(), invalid.event_id());
  auto persisted = run(find<ingest::v1::RawEventRecord>("psim.ingest.raw.v1", identity(0), historic.event_id()));
  EXPECT_EQ(persisted.envelope().tenant_id(), kTenant);
  EXPECT_EQ(persisted.raw_event().gateway_session_id(), stream.registered.session_id());
  EXPECT_FALSE(persisted.raw_event().live_observation());
  EXPECT_EQ(persisted.envelope().occurred_at().seconds(), historic.occurred_at().seconds());
  EXPECT_TRUE(stream.heartbeat());
  const auto audit = run(find<psim::audit::v1::AuditEventRecord>("psim.audit.v1", identity(0), "connector.connected"));
  EXPECT_EQ(audit.audit_entry().details().at("session_id"), stream.registered.session_id());
  EXPECT_EQ(audit.audit_entry().outcome(), psim::audit::v1::AUDIT_OUTCOME_SUCCESS);
  EXPECT_FALSE(audit.envelope().causation_id().empty());
}

TEST(GatewayNetwork, VersionedIdempotentAdministrationAndRoleChecks) {
  auto registration = enroll(1);
  wire::ManageRequest request;
  request.set_request_id(p::stream::uuid7());
  request.set_action(wire::REGISTRATION_ACTION_ROTATE);
  request.mutable_connector()->set_connector_id(identity(1));
  request.set_expected_version(registration.connector().version());
  request.set_certificate_pem(read(prefix(1, true) + ".pem"));
  wire::ManageResponse first;
  wire::ManageResponse second;
  const auto denied = manage(request, &first, 0, true);
  EXPECT_EQ(denied.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  google::rpc::Status details;
  ASSERT_TRUE(details.ParseFromString(denied.error_details()));
  ASSERT_EQ(details.details_size(), 1);
  google::rpc::ErrorInfo info;
  ASSERT_TRUE(details.details(0).UnpackTo(&info));
  EXPECT_EQ(info.reason(), "AUTH_FORBIDDEN");
  EXPECT_EQ(info.domain(), "psim");
  const auto audit = run(find<psim::audit::v1::AuditEventRecord>("psim.audit.v1", identity(1), "access.denied"));
  EXPECT_EQ(audit.audit_entry().outcome(), psim::audit::v1::AUDIT_OUTCOME_DENIED);
  EXPECT_EQ(audit.audit_entry().details().at("requested_action"), "connector.manage");
  ASSERT_TRUE(manage(request, &first).ok());
  ASSERT_TRUE(manage(request, &second, 1).ok());
  EXPECT_EQ(first.SerializeAsString(), second.SerializeAsString());
  request.set_fingerprint(std::string(64, 'a'));
  EXPECT_EQ(manage(request, &second).error_code(), grpc::StatusCode::ABORTED);
}

TEST(GatewayNetwork, RevocationClosesActiveTransportAcrossGatewaysAndBlocksReuse) {
  auto registration = enroll(2);
  Stream old(2);
  ASSERT_TRUE(old.connect());
  wire::ManageRequest request;
  request.set_request_id(p::stream::uuid7());
  request.set_action(wire::REGISTRATION_ACTION_ROTATE);
  request.mutable_connector()->set_connector_id(identity(2));
  request.set_expected_version(registration.connector().version());
  request.set_certificate_pem(read(prefix(2, true) + ".pem"));
  wire::ManageResponse rotated;
  ASSERT_TRUE(manage(request, &rotated, 1).ok());
  EXPECT_TRUE(old.heartbeat());
  request.Clear();
  request.set_request_id(p::stream::uuid7());
  request.set_action(wire::REGISTRATION_ACTION_REVOKE);
  request.mutable_connector()->set_connector_id(identity(2));
  request.set_expected_version(rotated.connector().version());
  request.set_fingerprint(registration.connector().certificates(0).sha256());
  wire::ManageResponse revoked;
  ASSERT_TRUE(manage(request, &revoked, 1).ok());
  wire::SessionResponse response;
  EXPECT_FALSE(old.stream->Read(&response));
  Stream refused(2, 1);
  EXPECT_FALSE(refused.connect());
  Stream replacement(2, 1, true);
  ASSERT_TRUE(replacement.connect());
  EXPECT_TRUE(replacement.heartbeat());
  request.set_request_id(p::stream::uuid7());
  request.set_expected_version(revoked.connector().version());
  request.set_fingerprint(revoked.connector().certificates(0).sha256());
  ASSERT_TRUE(manage(request, &revoked, 0).ok());
  EXPECT_FALSE(replacement.stream->Read(&response));
}

TEST(GatewayNetwork, InspectionVersionConflictDisableAndRecoveryRole) {
  auto registration = enroll(7);
  Stream active(7);
  ASSERT_TRUE(active.connect());
  wire::ManageRequest request;
  request.set_request_id(p::stream::uuid7());
  request.set_action(wire::REGISTRATION_ACTION_INSPECT);
  request.mutable_connector()->set_connector_id(identity(7));
  wire::ManageResponse inspected;
  ASSERT_TRUE(manage(request, &inspected, 1).ok());
  EXPECT_EQ(inspected.connector().version(), registration.connector().version());
  request.set_action(wire::REGISTRATION_ACTION_RECOVER);
  request.set_certificate_pem(read(prefix(7, true) + ".pem"));
  request.set_expected_version(registration.connector().version());
  EXPECT_EQ(manage(request, &inspected).error_code(), grpc::StatusCode::PERMISSION_DENIED);
  request.set_action(wire::REGISTRATION_ACTION_DISABLE);
  request.set_expected_version(registration.connector().version() + 1);
  EXPECT_EQ(manage(request, &inspected).error_code(), grpc::StatusCode::ABORTED);
  request.set_expected_version(registration.connector().version());
  ASSERT_TRUE(manage(request, &inspected, 1).ok());
  EXPECT_EQ(inspected.connector().state(), catalog::v1::RESOURCE_STATE_DISABLED);
  wire::SessionResponse response;
  EXPECT_FALSE(active.stream->Read(&response));
  Stream refused(7, 1);
  EXPECT_FALSE(refused.connect());
}

TEST(GatewayNetwork, SnapshotCannotOverwriteNewerLiveCursor) {
  enroll(3);
  Stream stream(3);
  ASSERT_TRUE(stream.connect());
  auto current = event(10);
  current.set_live_observation(true);
  current.set_observation_session_id(stream.registered.session_id());
  ASSERT_EQ(stream.batch({current}).accepted(), 1);
  wire::SessionRequest request;
  auto* report = request.mutable_state_report();
  report->set_snapshot(true);
  report->set_snapshot_id(p::stream::uuid7());
  report->set_complete(true);
  auto* observation = report->add_observations();
  observation->mutable_cursor()->set_source_ref(current.source_ref());
  observation->mutable_cursor()->set_source_epoch(1);
  observation->mutable_cursor()->set_source_seq(5);
  *observation->mutable_observed_at() = Util::GetCurrentTime();
  observation->set_availability(wire::DEVICE_AVAILABILITY_ONLINE);
  ASSERT_TRUE(stream.stream->Write(request));
  ASSERT_TRUE(stream.heartbeat());
  auto state =
      run(find<ingest::v1::GatewayStateRecord>("psim.ingest.state.v1", identity(3), stream.registered.session_id()));
  EXPECT_TRUE(state.state_report().complete());
  EXPECT_EQ(state.state_report().observations_size(), 0);
  auto persisted = run(find<ingest::v1::RawEventRecord>("psim.ingest.raw.v1", identity(3), current.event_id()));
  EXPECT_TRUE(persisted.raw_event().live_observation());
}

TEST(GatewayNetwork, RateLimitDelaysAcknowledgementWithoutLosingEvents) {
  enroll(4, 10);
  Stream stream(4);
  ASSERT_TRUE(stream.connect());
  std::vector<wire::ConnectorEvent> batch;
  for (std::uint64_t n = 1; n <= 10; ++n) {
    batch.push_back(event(n));
  }
  EXPECT_EQ(stream.batch(batch).accepted(), 10);
  const auto start = std::chrono::steady_clock::now();
  for (auto& item : batch) {
    item.set_event_id(p::stream::uuid7());
    item.set_source_seq(item.source_seq() + 10);
  }
  EXPECT_EQ(stream.batch(batch).accepted(), 10);
  EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(700));
}

TEST(GatewayNetwork, CommandOwnershipReceiptAndOutcome) {
  enroll(5);
  Stream stream(5, 1);
  ASSERT_TRUE(stream.connect());
  response::v1::CommandDeliveryRecord record;
  auto* metadata = record.mutable_envelope();
  metadata->set_message_id(p::stream::uuid7());
  metadata->set_tenant_id(std::string(kTenant));
  auto* command = record.mutable_command_delivery();
  command->set_command_id(p::stream::uuid7());
  command->set_connector_id(identity(5));
  command->set_capability("unlock");
  command->set_device_external_id("door-1");
  *command->mutable_deadline_at() = Util::SecondsToTimestamp(Util::GetCurrentTime().seconds() + 30);
  auto foreign = record;
  foreign.mutable_envelope()->set_tenant_id(p::stream::uuid7());
  foreign.mutable_command_delivery()->set_command_id(p::stream::uuid7());
  run(send_command(foreign));
  auto expired = record;
  expired.mutable_command_delivery()->set_command_id(p::stream::uuid7());
  *expired.mutable_command_delivery()->mutable_deadline_at() =
      Util::SecondsToTimestamp(Util::GetCurrentTime().seconds() - 1);
  run(send_command(expired));
  run(send_command(record));
  wire::SessionResponse received;
  ASSERT_TRUE(stream.next(&received, wire::SessionResponse::kCommand));
  EXPECT_EQ(received.command().command_id(), command->command_id());
  wire::SessionRequest ack;
  ack.mutable_command_ack()->set_command_id(command->command_id());
  ASSERT_TRUE(stream.stream->Write(ack));
  ack.Clear();
  ack.mutable_command_result()->set_command_id(command->command_id());
  ack.mutable_command_result()->set_success(true);
  ASSERT_TRUE(stream.stream->Write(ack));
  ASSERT_TRUE(stream.heartbeat());
  auto outcome =
      run(find<response::v1::CommandResultRecord>("psim.commands.results.v1", identity(5), command->command_id()));
  EXPECT_TRUE(outcome.command_outcome().success());
  EXPECT_EQ(outcome.envelope().tenant_id(), kTenant);
}

TEST(GatewayNetwork, UnissuedCommandResponseClosesTransport) {
  enroll(9);
  Stream stream(9);
  ASSERT_TRUE(stream.connect());
  wire::SessionRequest request;
  request.mutable_command_ack()->set_command_id(p::stream::uuid7());
  ASSERT_TRUE(stream.stream->Write(request));
  wire::SessionResponse response;
  EXPECT_FALSE(stream.stream->Read(&response));
}

TEST(GatewayNetwork, OversizedCreditWindowClosesTransportWithoutAcknowledgement) {
  enroll(10, 1);
  Stream stream(10);
  ASSERT_TRUE(stream.connect());
  wire::SessionRequest request;
  request.mutable_event_batch()->set_batch_seq(1);
  *request.mutable_event_batch()->add_events() = event(1);
  *request.mutable_event_batch()->add_events() = event(2);
  ASSERT_TRUE(stream.stream->Write(request));
  wire::SessionResponse response;
  EXPECT_FALSE(stream.stream->Read(&response));
}

namespace {
asio::awaitable<void> lose_hint(std::string connector) {
  auto connection =
      co_await p::redis::Connection::connect({.endpoint = {.host = "valkey"},
                                              .credentials = {.password = read(".cache/event-flow/redis.secret")},
                                              .tls = {.enabled = false}});
  if (!connection) {
    throw std::runtime_error("test Redis connection failed");
  }
  auto key = p::redis::Keyspace("connector-gateway", std::string(kTenant)).key("session", connector);
  if (!key) {
    throw std::runtime_error("test Redis key invalid");
  }
  auto deleted = co_await (*connection)->execute({"DEL", *key});
  if (!deleted) {
    throw std::runtime_error("test Redis state loss failed");
  }
}

}  // namespace

TEST(GatewayNetwork, LostRedisHintCannotCreateAnotherAuthoritativeOwner) {
  enroll(8);
  Stream owner(8);
  ASSERT_TRUE(owner.connect());
  run(lose_hint(identity(8)));
  Stream competitor(8, 1);
  EXPECT_FALSE(competitor.connect());
  EXPECT_TRUE(owner.heartbeat());
}

TEST(GatewayNetwork, ThreeMissedHeartbeatsCloseSession) {
  enroll(6);
  Stream stream(6);
  ASSERT_TRUE(stream.connect());
  const auto start = std::chrono::steady_clock::now();
  const auto connected = run(find<ingest::v1::GatewayStateRecord>(
      "psim.ingest.state.v1", identity(6), stream.registered.session_id(), StatePayload::kOnline));
  EXPECT_EQ(connected.connection().quality(), "unknown");
  wire::SessionResponse response;
  while (stream.stream->Read(&response)) {
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_GE(elapsed, std::chrono::seconds(14));
  EXPECT_LT(elapsed, std::chrono::seconds(18));
  const auto disconnected = run(find<ingest::v1::GatewayStateRecord>(
      "psim.ingest.state.v1", identity(6), stream.registered.session_id(), StatePayload::kOffline));
  EXPECT_EQ(disconnected.connection().quality(), "stale");
  EXPECT_GT(disconnected.envelope().sequence(), connected.envelope().sequence());
}
}  // namespace psim::connector_gateway::testing

int main(int argc, char** argv) {
  const psim::platform::connector::GrpcProcess grpc;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
