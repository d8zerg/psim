#pragma once

#include <boost/asio/awaitable.hpp>
#include <google/protobuf/message.h>

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/wire_format.hpp"

namespace psim::platform::kafka {

namespace asio = boost::asio;

/// Values of a topic: record type T in the Confluent wire format (ADR-004). The writer schema id
/// is resolved once at start, so the registry is off the hot path (ADR-003). Reading accepts any
/// schema id: compatibility of all versions is enforced when they are registered (FF-04).
template <typename T>
  requires std::derived_from<T, google::protobuf::Message>
class ProtobufSerde {
 public:
  ProtobufSerde(std::int32_t writer_id) : writer_id_(writer_id), indexes_(wire::message_indexes(*T::descriptor())) {}

  /// Serde of topic `topic`: subject "<topic>-value" (TopicNameStrategy).
  static asio::awaitable<Result<ProtobufSerde>> create(SchemaRegistry* registry, std::string topic) {
    auto id = co_await registry->latest_id(topic + "-value");
    if (!id) {
      co_return std::unexpected(std::move(id.error()).context("serde of " + topic));
    }
    co_return ProtobufSerde(*id);
  }

  [[nodiscard]] std::int32_t writer_id() const noexcept { return writer_id_; }

  [[nodiscard]] std::string serialize(const T& message) const {
    std::string out;
    out.reserve(message.ByteSizeLong() + 8);
    wire::write_header(out, writer_id_, indexes_);
    message.AppendToString(&out);
    return out;
  }

  /// A malformed value or one of another type is PROCESSING_PAYLOAD_INVALID (dead letter topic).
  [[nodiscard]] Result<T> deserialize(std::string_view value) const {
    auto frame = wire::parse(value);
    if (!frame) {
      return std::unexpected(std::move(frame.error()));
    }
    if (frame->indexes != indexes_) {
      return fail(ErrorCode::kProcessingPayloadInvalid,
                  "value is not a " + std::string(T::descriptor()->full_name()) + " (message indexes differ)");
    }
    T message;
    if (!message.ParseFromArray(frame->body.data(), static_cast<int>(frame->body.size()))) {
      return fail(ErrorCode::kProcessingPayloadInvalid,
                  "value is not a valid " + std::string(T::descriptor()->full_name()));
    }
    return message;
  }

 private:
  std::int32_t writer_id_;
  std::vector<std::int32_t> indexes_;
};

}  // namespace psim::platform::kafka
