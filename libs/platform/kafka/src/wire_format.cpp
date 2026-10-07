#include "psim/platform/kafka/wire_format.hpp"

#include <google/protobuf/descriptor.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::kafka::wire {

namespace {

constexpr std::size_t kHeader = 5;
constexpr int kMaxVarintBytes = 5;
// Bound of the index path: proto files do not nest types deeper.
constexpr std::int32_t kMaxIndexes = 64;

void write_varint(std::string& out, std::int32_t value) {
  // Zigzag, as the Confluent serializers (ByteUtils.writeVarint).
  auto zigzag = (static_cast<std::uint32_t>(value) << 1U) ^ static_cast<std::uint32_t>(value >> 31);
  while (zigzag >= 0x80U) {
    out.push_back(static_cast<char>((zigzag & 0x7FU) | 0x80U));
    zigzag >>= 7U;
  }
  out.push_back(static_cast<char>(zigzag));
}

bool read_varint(std::string_view& in, std::int32_t& value) {
  std::uint32_t raw = 0;
  for (int i = 0; i < kMaxVarintBytes; ++i) {
    if (in.empty()) {
      return false;
    }
    const auto byte = static_cast<std::uint8_t>(in.front());
    in.remove_prefix(1);
    raw |= static_cast<std::uint32_t>(byte & 0x7FU) << (7U * static_cast<unsigned>(i));
    if ((byte & 0x80U) == 0) {
      value = static_cast<std::int32_t>((raw >> 1U) ^ (~(raw & 1U) + 1U));
      return true;
    }
  }
  return false;
}

std::unexpected<Error> malformed(std::string_view what) {
  return fail(ErrorCode::kProcessingPayloadInvalid, "malformed Confluent wire format: " + std::string(what));
}

}  // namespace

std::vector<std::int32_t> message_indexes(const google::protobuf::Descriptor& descriptor) {
  std::vector<std::int32_t> path;
  for (const auto* type = &descriptor; type != nullptr; type = type->containing_type()) {
    path.push_back(type->index());
  }
  std::ranges::reverse(path);
  return path;
}

void write_header(std::string& out, std::int32_t schema_id, std::span<const std::int32_t> indexes) {
  out.push_back(kMagic);
  const auto id = static_cast<std::uint32_t>(schema_id);
  for (const unsigned shift : {24U, 16U, 8U, 0U}) {
    out.push_back(static_cast<char>((id >> shift) & 0xFFU));
  }
  if (indexes.size() == 1 && indexes.front() == 0) {
    out.push_back(0);  // the common case: the first type of the file
    return;
  }
  write_varint(out, static_cast<std::int32_t>(indexes.size()));
  for (const auto index : indexes) {
    write_varint(out, index);
  }
}

Result<Frame> parse(std::string_view value) {
  if (value.size() < kHeader + 1) {
    return malformed("shorter than its header");
  }
  if (value.front() != kMagic) {
    return malformed("unknown magic byte");
  }
  Frame frame;
  std::uint32_t id = 0;
  for (std::size_t i = 1; i < kHeader; ++i) {
    id = (id << 8U) | static_cast<std::uint8_t>(value[i]);
  }
  frame.schema_id = static_cast<std::int32_t>(id);
  std::string_view rest = value.substr(kHeader);
  std::int32_t count = 0;
  if (!read_varint(rest, count) || count < 0 || count > kMaxIndexes) {
    return malformed("bad message index count");
  }
  if (count == 0) {
    frame.indexes = {0};
  }
  for (std::int32_t i = 0; i < count; ++i) {
    std::int32_t index = 0;
    if (!read_varint(rest, index) || index < 0) {
      return malformed("bad message index");
    }
    frame.indexes.push_back(index);
  }
  frame.body = rest;
  return frame;
}

}  // namespace psim::platform::kafka::wire
