#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The Kafka layer (step 2.3, ADR-041) wraps librdkafka; its handles stay out of the public headers
// except as opaque pointers.
struct rd_kafka_message_s;  // NOLINT(readability-identifier-naming): librdkafka C type

namespace psim::platform::async {
class BlockingPool;
}  // namespace psim::platform::async

namespace psim::platform::log {
class Logger;
}  // namespace psim::platform::log

namespace psim::platform::kafka {

class KafkaMetrics;

/// Services of the process a client uses; every one is optional unless a method says otherwise.
struct Dependencies {
  /// Blocking librdkafka calls: transactions, synchronous commits (ADR-016, item 4).
  async::BlockingPool* blocking = nullptr;
  KafkaMetrics* metrics = nullptr;
  /// Broker errors, rebalances, retries, dead letters.
  log::Logger* logger = nullptr;
};

/// Headers of a message in their order; a name may repeat (W3C trace context, DLQ, psim-ts-<stage>).
using Headers = std::vector<std::pair<std::string, std::string>>;

/// Connection settings shared by producers and consumers.
struct ClientOptions {
  /// Bootstrap brokers, "host:port[,host:port]".
  std::string bootstrap{};
  /// Client id in broker logs and quotas: "<service>-<instance>".
  std::string client_id{};
  /// Additional librdkafka properties: tuning and, from step 2.6, security. They override the
  /// defaults of the layer except the ones the delivery semantics depend on (ADR-041).
  std::map<std::string, std::string> properties{};
};

struct TopicPartition {
  std::string topic{};
  std::int32_t partition = 0;

  friend auto operator<=>(const TopicPartition&, const TopicPartition&) = default;
};

/// Next offset to read per partition: what a commit stores (the last processed offset + 1).
using Offsets = std::map<TopicPartition, std::int64_t>;

/// An outgoing message.
struct Record {
  std::string topic{};
  std::string key{};
  std::string value{};
  Headers headers{};
  /// Explicit partition; by default the partitioner hashes the key (murmur2, as the Java client).
  std::optional<std::int32_t> partition{};
};

/// A consumed message. Owns the librdkafka message; views stay valid while the Message lives.
class Message {
 public:
  explicit Message(rd_kafka_message_s* message) noexcept : message_(message) {}

  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;

  Message(Message&& other) noexcept : message_(std::exchange(other.message_, nullptr)) {}

  Message& operator=(Message&& other) noexcept;
  ~Message();

  [[nodiscard]] std::string_view topic() const noexcept;
  [[nodiscard]] std::int32_t partition() const noexcept;
  [[nodiscard]] std::int64_t offset() const noexcept;
  [[nodiscard]] std::string_view key() const noexcept;
  [[nodiscard]] std::string_view value() const noexcept;
  /// Producer (CreateTime) or broker (LogAppendTime) timestamp, when the message has one.
  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> timestamp() const noexcept;
  /// Value of the last header with this name.
  [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const;
  [[nodiscard]] Headers headers() const;

  [[nodiscard]] TopicPartition topic_partition() const { return {std::string(topic()), partition()}; }

 private:
  rd_kafka_message_s* message_;
};

/// Where a produced message landed.
struct Delivery {
  std::int32_t partition = 0;
  std::int64_t offset = 0;
};

}  // namespace psim::platform::kafka
