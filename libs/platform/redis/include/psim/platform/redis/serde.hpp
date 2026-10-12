#pragma once

#include <google/protobuf/message_lite.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::platform::redis {
constexpr std::size_t kMaxPayloadBytes = std::size_t{1024} * 1024;
enum class Format : std::uint8_t { kBytes = 0, kProtobuf = 1 };

struct Envelope {
  Format format = Format::kBytes;
  std::string schema{};
  std::string payload{};
};

/// Versioned binary envelope; unknown versions, formats and schemas fail explicitly.
[[nodiscard]] Result<std::string> encode(const Envelope& envelope);
[[nodiscard]] Result<Envelope> decode(std::string_view bytes);
[[nodiscard]] Result<std::string> encode_protobuf(const google::protobuf::MessageLite& message);

template <typename Message>
Result<Message> decode_protobuf(std::string_view bytes) {
  auto envelope = decode(bytes);
  if (!envelope) {
    return std::unexpected(envelope.error());
  }
  Message message;
  if (envelope->format != Format::kProtobuf || envelope->schema != message.GetTypeName()
      || !message.ParseFromArray(envelope->payload.data(), static_cast<int>(envelope->payload.size()))) {
    return fail(ErrorCode::kCommonInvalidArgument, "invalid Redis Protobuf value or schema");
  }
  return message;
}

/// Length-delimited binary-safe key segments prevent collisions and isolate tenants.
class Keyspace {
 public:
  Keyspace(std::string service, std::string tenant) : service_(std::move(service)), tenant_(std::move(tenant)) {}

  [[nodiscard]] Result<std::string> key(std::string_view kind, std::string_view id) const;

 private:
  std::string service_{};
  std::string tenant_{};
};
}  // namespace psim::platform::redis
