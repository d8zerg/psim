#include "psim/platform/kafka/serde.hpp"

#include <google/protobuf/descriptor.pb.h>
#include <gtest/gtest.h>

#include <string>

#include "psim/common/v1/envelope.pb.h"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/wire_format.hpp"

namespace psim::platform::kafka {
namespace {

using common::v1::Envelope;

TEST(ProtobufSerde, RoundTripsAMessageWithTheWriterId) {
  const ProtobufSerde<Envelope> serde(42);
  Envelope envelope;
  envelope.set_message_id("0190f1a2-0000-7000-8000-000000000001");
  envelope.set_sequence(7);

  const auto value = serde.serialize(envelope);
  const auto frame = wire::parse(value);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->schema_id, 42);

  const auto back = serde.deserialize(value);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->message_id(), envelope.message_id());
  EXPECT_EQ(back->sequence(), 7U);
  EXPECT_EQ(serde.writer_id(), 42);
}

TEST(ProtobufSerde, AcceptsOtherSchemaIdsOfTheSameType) {
  const ProtobufSerde<Envelope> writer(1);
  const ProtobufSerde<Envelope> reader(2);
  Envelope envelope;
  envelope.set_tenant_id("t");
  EXPECT_TRUE(reader.deserialize(writer.serialize(envelope)).has_value());
}

TEST(ProtobufSerde, RejectsAnotherTypeAndGarbage) {
  const ProtobufSerde<google::protobuf::DescriptorProto::ExtensionRange> other(1);
  const ProtobufSerde<Envelope> serde(1);
  const auto wrong_type = serde.deserialize(other.serialize({}));
  ASSERT_FALSE(wrong_type.has_value());
  EXPECT_EQ(wrong_type.error().code(), ErrorCode::kProcessingPayloadInvalid);

  const std::string garbage("\x00\x00\x00\x00\x01\x00\xFF\xFF\xFF", 9);
  const auto invalid = serde.deserialize(garbage);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().code(), ErrorCode::kProcessingPayloadInvalid);

  EXPECT_FALSE(serde.deserialize("plain json").has_value());
}

}  // namespace
}  // namespace psim::platform::kafka
