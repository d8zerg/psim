#pragma once

#include <google/protobuf/descriptor.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/error.hpp"

namespace psim::platform::kafka::wire {

/// Confluent wire format of Protobuf values (ADR-004): magic byte 0, schema id (4 bytes, big
/// endian), message indexes (zigzag varints: count, then the path of the message type in its
/// file; the first top-level type is the single byte 0), then the Protobuf body.
inline constexpr char kMagic = 0;

struct Frame {
  std::int32_t schema_id = 0;
  std::vector<std::int32_t> indexes;
  std::string_view body;
};

/// Path of a message type in its .proto file: indexes of the top-level type and the nested ones.
[[nodiscard]] std::vector<std::int32_t> message_indexes(const google::protobuf::Descriptor& descriptor);

/// Append the header of a framed value to `out`; the body follows it.
void write_header(std::string& out, std::int32_t schema_id, std::span<const std::int32_t> indexes);

/// Split a framed value; an error names what is malformed (PROCESSING_PAYLOAD_INVALID).
[[nodiscard]] Result<Frame> parse(std::string_view value);

}  // namespace psim::platform::kafka::wire
