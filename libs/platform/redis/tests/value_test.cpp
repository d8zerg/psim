#include <google/protobuf/empty.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <gtest/gtest.h>

#include <string>

#include "psim/platform/redis/connection.hpp"
#include "psim/platform/redis/serde.hpp"

namespace psim::platform::redis {
TEST(RedisSerde, BinaryEnvelopePreservesEmptyAndNonUtf8Values) {
  for (const auto& payload : {std::string{}, std::string("a\0\xff", 3)}) {
    auto bytes = encode({.payload = payload});
    ASSERT_TRUE(bytes);
    auto envelope = decode(*bytes);
    ASSERT_TRUE(envelope);
    EXPECT_EQ(envelope->format, Format::kBytes);
    EXPECT_EQ(envelope->payload, payload);
  }
}

TEST(RedisSerde, MalformedAndOversizedEnvelopesAreRejected) {
  EXPECT_FALSE(decode(""));
  EXPECT_FALSE(encode({.payload = std::string(kMaxPayloadBytes + 1, 'a')}));
  EXPECT_FALSE(encode({.format = Format::kProtobuf}));
  EXPECT_FALSE(encode({.schema = "unexpected"}));
  EXPECT_FALSE(encode({.format = Format::kProtobuf, .schema = std::string(513, 's')}));
  auto good = encode({.payload = "hello"}).value();
  good[4] = 2;
  EXPECT_FALSE(decode(good));
  good[4] = 1;
  good[5] = 2;
  EXPECT_FALSE(decode(good));
  good[5] = 1;
  good[6] = 127;
  EXPECT_FALSE(decode(good));
  EXPECT_FALSE(decode(std::string("PSIM\1\0\0\0", 8) + std::string(kMaxPayloadBytes + 1, 'a')));
}

TEST(RedisSerde, ProtobufSchemaAndPayloadAreValidated) {
  google::protobuf::StringValue message;
  message.set_value("payload");
  auto bytes = encode_protobuf(message);
  ASSERT_TRUE(bytes);
  auto decoded = decode_protobuf<google::protobuf::StringValue>(*bytes);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded->value(), "payload");
  EXPECT_FALSE(decode_protobuf<google::protobuf::Empty>(*bytes));
  EXPECT_FALSE(decode_protobuf<google::protobuf::StringValue>(encode({.payload = "bytes"}).value()));
  EXPECT_FALSE(decode_protobuf<google::protobuf::StringValue>(
      encode({Format::kProtobuf, "google.protobuf.StringValue", "\xff"}).value()));
  message.set_value(std::string(kMaxPayloadBytes + 1, 'x'));
  EXPECT_FALSE(encode_protobuf(message));
}

TEST(RedisKeys, LengthDelimitersPreventSegmentAndTenantCollisions) {
  const Keyspace first("a:b", "c");
  const Keyspace second("a", "b:c");
  EXPECT_NE(first.key("cache", "x"), second.key("cache", "x"));
  EXPECT_NE(Keyspace("svc", "one").key("cache", "x"), Keyspace("svc", "two").key("cache", "x"));
  EXPECT_NE(first.key("cache", "x"), first.key("session", "x"));
  EXPECT_TRUE(first.key("cache", std::string("\0\xff", 2)));
  EXPECT_FALSE(first.key("cache", ""));
  EXPECT_FALSE(first.key("", "x"));
  EXPECT_FALSE(Keyspace("", "tenant").key("cache", "x"));
  EXPECT_FALSE(first.key("cache", std::string(513, 'x')));
}

TEST(RedisReply, IntegerConversionRejectsWrongTypeOverflowAndTrailingData) {
  EXPECT_EQ((Reply{Reply::Kind::kInteger, "-42", {}}).integer().value(), -42);
  EXPECT_FALSE((Reply{Reply::Kind::kString, "42", {}}).integer());
  EXPECT_FALSE((Reply{Reply::Kind::kInteger, "42x", {}}).integer());
  EXPECT_FALSE((Reply{Reply::Kind::kInteger, "9223372036854775808", {}}).integer());
}
}  // namespace psim::platform::redis
