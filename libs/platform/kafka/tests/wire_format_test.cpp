#include "psim/platform/kafka/wire_format.hpp"

#include <google/protobuf/descriptor.pb.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "psim/common/v1/envelope.pb.h"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::kafka::wire {
namespace {

TEST(WireFormat, FirstTypeOfTheFileIsTheSingleZeroByte) {
  std::string out;
  const std::vector<std::int32_t> first{0};
  write_header(out, 0x01020304, first);
  EXPECT_EQ(out, std::string("\x00\x01\x02\x03\x04\x00", 6));
}

TEST(WireFormat, OtherPathsAreZigzagVarints) {
  std::string out;
  const std::vector<std::int32_t> nested{1, 2};
  write_header(out, 7, nested);
  // count 2 -> 4, index 1 -> 2, index 2 -> 4 (Confluent ByteUtils.writeVarint)
  EXPECT_EQ(out, std::string("\x00\x00\x00\x00\x07\x04\x02\x04", 8));
}

TEST(WireFormat, ParsesWhatItWrites) {
  const std::vector<std::int32_t> path{3, 70, 0};
  std::string value;
  write_header(value, 123456, path);
  value += "body";
  const auto frame = parse(value);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->schema_id, 123456);
  EXPECT_EQ(frame->indexes, path);
  EXPECT_EQ(frame->body, "body");
}

TEST(WireFormat, SingleZeroByteMeansTheFirstType) {
  const auto frame = parse(std::string("\x00\x00\x00\x00\x01\x00", 6));
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->indexes, std::vector<std::int32_t>{0});
  EXPECT_TRUE(frame->body.empty());
}

TEST(WireFormat, RejectsMalformedValues) {
  for (const std::string& value :
       {std::string("\x00\x00\x00", 3), std::string("\x01\x00\x00\x00\x01\x00", 6),
        std::string("\x00\x00\x00\x00\x01\x80\x80\x80\x80\x80", 10), std::string("\x00\x00\x00\x00\x01\x04\x02", 7),
        std::string("\x00\x00\x00\x00\x01\x01", 6)}) {
    const auto frame = parse(value);
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code(), ErrorCode::kProcessingPayloadInvalid);
  }
}

TEST(WireFormat, MessageIndexesFollowNesting) {
  using google::protobuf::DescriptorProto;
  EXPECT_EQ(message_indexes(*common::v1::Envelope::descriptor()),
            std::vector<std::int32_t>{common::v1::Envelope::descriptor()->index()});
  const auto* range = DescriptorProto::ExtensionRange::descriptor();
  EXPECT_EQ(message_indexes(*range),
            (std::vector<std::int32_t>{DescriptorProto::descriptor()->index(), range->index()}));
}

}  // namespace
}  // namespace psim::platform::kafka::wire
