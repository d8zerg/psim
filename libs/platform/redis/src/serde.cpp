#include "psim/platform/redis/serde.hpp"

#include <google/protobuf/message_lite.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::redis {
namespace {
constexpr std::string_view kMagic = "PSIM";
constexpr unsigned char kVersion = 1;
constexpr std::size_t kHeaderBytes = 8;
constexpr std::size_t kMaxSchemaBytes = 512;
}  // namespace

Result<std::string> encode(const Envelope& envelope) {
  if (envelope.payload.size() > kMaxPayloadBytes || envelope.schema.size() > kMaxSchemaBytes
      || (envelope.format != Format::kBytes && envelope.format != Format::kProtobuf)
      || (envelope.format == Format::kBytes && !envelope.schema.empty())
      || (envelope.format == Format::kProtobuf && envelope.schema.empty())) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis value envelope");
  }
  std::string bytes(kMagic);
  bytes.push_back(static_cast<char>(kVersion));
  bytes.push_back(static_cast<char>(envelope.format));
  bytes.push_back(static_cast<char>(envelope.schema.size() >> 8U));
  bytes.push_back(static_cast<char>(envelope.schema.size() & 255U));
  bytes += envelope.schema;
  bytes += envelope.payload;
  return bytes;
}

Result<Envelope> decode(std::string_view bytes) {
  if (bytes.size() < kHeaderBytes || bytes.substr(0, 4) != kMagic || static_cast<unsigned char>(bytes[4]) != kVersion) {
    return fail(ErrorCode::kCommonInvalidArgument, "unsupported Redis value envelope");
  }
  const auto format = static_cast<unsigned char>(bytes[5]);
  const auto schema_size =
      (static_cast<std::size_t>(static_cast<unsigned char>(bytes[6])) << 8U) | static_cast<unsigned char>(bytes[7]);
  if (format > 1 || schema_size > kMaxSchemaBytes || bytes.size() < kHeaderBytes + schema_size
      || bytes.size() - kHeaderBytes - schema_size > kMaxPayloadBytes || (format == 0 && schema_size != 0)
      || (format == 1 && schema_size == 0)) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis value envelope");
  }
  return Envelope{format == 0 ? Format::kBytes : Format::kProtobuf,
                  std::string(bytes.substr(kHeaderBytes, schema_size)),
                  std::string(bytes.substr(kHeaderBytes + schema_size))};
}

Result<std::string> encode_protobuf(const google::protobuf::MessageLite& message) {
  if (message.ByteSizeLong() > kMaxPayloadBytes) {
    return fail(ErrorCode::kCommonInvalidArgument, "Redis Protobuf value too large");
  }
  std::string payload;
  if (!message.SerializeToString(&payload)) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis Protobuf value");
  }
  return encode({Format::kProtobuf, std::string(message.GetTypeName()), std::move(payload)});
}

Result<std::string> Keyspace::key(std::string_view kind, std::string_view id) const {
  const std::array<std::string_view, 4> segments{service_, tenant_, kind, id};
  std::string key = "psim:";
  for (const auto segment : segments) {
    if (segment.empty() || segment.size() > 512) {
      return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis key segment");
    }
    key += std::to_string(segment.size()) + ":";
    key += segment;
  }
  return key;
}
}  // namespace psim::platform::redis
