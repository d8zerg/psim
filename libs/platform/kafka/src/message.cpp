#include <librdkafka/rdkafka.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/kafka/client.hpp"

namespace psim::platform::kafka {

namespace {

std::string_view bytes(const void* data, std::size_t size) {
  return data == nullptr ? std::string_view{} : std::string_view(static_cast<const char*>(data), size);
}

}  // namespace

Message& Message::operator=(Message&& other) noexcept {
  if (this != &other) {
    if (message_ != nullptr) {
      rd_kafka_message_destroy(message_);
    }
    message_ = std::exchange(other.message_, nullptr);
  }
  return *this;
}

Message::~Message() {
  if (message_ != nullptr) {
    rd_kafka_message_destroy(message_);
  }
}

std::string_view Message::topic() const noexcept {
  return rd_kafka_topic_name(message_->rkt);
}

std::int32_t Message::partition() const noexcept {
  return message_->partition;
}

std::int64_t Message::offset() const noexcept {
  return message_->offset;
}

std::string_view Message::key() const noexcept {
  return bytes(message_->key, message_->key_len);
}

std::string_view Message::value() const noexcept {
  return bytes(message_->payload, message_->len);
}

std::optional<std::chrono::system_clock::time_point> Message::timestamp() const noexcept {
  rd_kafka_timestamp_type_t type = RD_KAFKA_TIMESTAMP_NOT_AVAILABLE;
  const std::int64_t ms = rd_kafka_message_timestamp(message_, &type);
  if (type == RD_KAFKA_TIMESTAMP_NOT_AVAILABLE || ms < 0) {
    return std::nullopt;
  }
  return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

std::optional<std::string_view> Message::header(std::string_view name) const {
  rd_kafka_headers_t* headers = nullptr;  // NOLINT(misc-const-correctness): out-parameter of the C API
  if (rd_kafka_message_headers(message_, &headers) != RD_KAFKA_RESP_ERR_NO_ERROR) {
    return std::nullopt;
  }
  const std::string key(name);
  const void* value = nullptr;
  std::size_t size = 0;
  if (rd_kafka_header_get_last(headers, key.c_str(), &value, &size) != RD_KAFKA_RESP_ERR_NO_ERROR) {
    return std::nullopt;
  }
  return bytes(value, size);
}

Headers Message::headers() const {
  Headers result;
  rd_kafka_headers_t* headers = nullptr;  // NOLINT(misc-const-correctness): out-parameter of the C API
  if (rd_kafka_message_headers(message_, &headers) != RD_KAFKA_RESP_ERR_NO_ERROR) {
    return result;
  }
  const char* name = nullptr;
  const void* value = nullptr;
  std::size_t size = 0;
  for (std::size_t i = 0; rd_kafka_header_get_all(headers, i, &name, &value, &size) == RD_KAFKA_RESP_ERR_NO_ERROR;
       ++i) {
    result.emplace_back(name, bytes(value, size));
  }
  return result;
}

}  // namespace psim::platform::kafka
