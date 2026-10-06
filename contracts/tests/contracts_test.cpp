// Checks that generated contract code links and behaves as the contracts promise (ADR-004, ADR-026).

#include <google/protobuf/util/json_util.h>
#include <gtest/gtest.h>

#include <string>

#include "psim/common/v1/envelope.pb.h"
#include "psim/connector/v1/connector.grpc.pb.h"
#include "psim/incident/v1/events.pb.h"

namespace {

using psim::common::v1::ACTOR_KIND_SERVICE;
using psim::common::v1::Envelope;
using psim::incident::v1::INCIDENT_STATE_NEW;
using psim::incident::v1::IncidentEventRecord;

Envelope make_envelope() {
  Envelope envelope;
  envelope.set_message_id("01928f5e-7a3b-7c4d-8e9f-0a1b2c3d4e5f");
  envelope.set_tenant_id("01928f5e-0000-7000-8000-000000000001");
  envelope.mutable_occurred_at()->set_seconds(1'790'000'000);
  envelope.mutable_produced_at()->set_seconds(1'790'000'001);
  envelope.set_sequence(7);
  envelope.set_correlation_id(envelope.message_id());
  envelope.mutable_actor()->set_kind(ACTOR_KIND_SERVICE);
  envelope.mutable_actor()->set_id("incident-service");
  return envelope;
}

TEST(Contracts, TopicRecordRoundTripsInBinary) {
  IncidentEventRecord record;
  *record.mutable_envelope() = make_envelope();
  auto* incident = record.mutable_incident_created()->mutable_incident();
  incident->set_incident_id("01928f5e-7a3b-7c4d-8e9f-000000000042");
  incident->set_state(INCIDENT_STATE_NEW);
  incident->set_version(1);

  std::string wire;
  ASSERT_TRUE(record.SerializeToString(&wire));
  IncidentEventRecord decoded;
  ASSERT_TRUE(decoded.ParseFromString(wire));

  EXPECT_EQ(decoded.payload_case(), IncidentEventRecord::kIncidentCreated);
  EXPECT_EQ(decoded.envelope().sequence(), 7U);
  EXPECT_EQ(decoded.incident_created().incident().state(), INCIDENT_STATE_NEW);
}

TEST(Contracts, JsonUsesCanonicalProtobufMapping) {
  std::string json;
  ASSERT_TRUE(google::protobuf::util::MessageToJsonString(make_envelope(), &json).ok());

  // lowerCamelCase field names, int64 as strings, enums as names, RFC 3339 timestamps (ADR-026).
  EXPECT_NE(json.find(R"("messageId":"01928f5e-7a3b-7c4d-8e9f-0a1b2c3d4e5f")"), std::string::npos) << json;
  EXPECT_NE(json.find(R"("sequence":"7")"), std::string::npos) << json;
  EXPECT_NE(json.find(R"("kind":"ACTOR_KIND_SERVICE")"), std::string::npos) << json;
  EXPECT_NE(json.find(R"("occurredAt":"2026-09-21T)"), std::string::npos) << json;

  Envelope parsed;
  ASSERT_TRUE(google::protobuf::util::JsonStringToMessage(json, &parsed).ok());
  EXPECT_EQ(parsed.SerializeAsString(), make_envelope().SerializeAsString());
}

TEST(Contracts, ConnectorServiceIsGenerated) {
  const auto* service = psim::connector::v1::ConnectorGatewayService::service_full_name();
  EXPECT_STREQ(service, "psim.connector.v1.ConnectorGatewayService");
}

}  // namespace
