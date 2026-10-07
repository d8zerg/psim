#pragma once

// librdkafka handles, configuration, error mapping and the bridge of librdkafka event queues into
// Asio (ADR-041). Internal to the Kafka layer.

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <librdkafka/rdkafka.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"

namespace psim::platform::kafka::detail {

namespace asio = boost::asio;

struct KafkaDeleter {
  void operator()(rd_kafka_t* handle) const noexcept { rd_kafka_destroy(handle); }
};

using KafkaPtr = std::unique_ptr<rd_kafka_t, KafkaDeleter>;

struct QueueDeleter {
  void operator()(rd_kafka_queue_t* queue) const noexcept { rd_kafka_queue_destroy(queue); }
};

using QueuePtr = std::unique_ptr<rd_kafka_queue_t, QueueDeleter>;

struct ConfDeleter {
  void operator()(rd_kafka_conf_t* conf) const noexcept { rd_kafka_conf_destroy(conf); }
};

using ConfPtr = std::unique_ptr<rd_kafka_conf_t, ConfDeleter>;

struct ListDeleter {
  void operator()(rd_kafka_topic_partition_list_t* list) const noexcept { rd_kafka_topic_partition_list_destroy(list); }
};

using ListPtr = std::unique_ptr<rd_kafka_topic_partition_list_t, ListDeleter>;

struct ErrorDeleter {
  void operator()(rd_kafka_error_t* error) const noexcept { rd_kafka_error_destroy(error); }
};

using ErrorPtr = std::unique_ptr<rd_kafka_error_t, ErrorDeleter>;

using Properties = std::map<std::string, std::string, std::less<>>;

/// librdkafka configuration: `defaults` of the layer, then the client's properties, then `fixed`
/// - properties the delivery semantics depend on; overriding one of them is an error.
[[nodiscard]] Result<ConfPtr> make_conf(const ClientOptions& options, const Properties& defaults,
                                        const Properties& fixed);

/// Catalog error of a librdkafka error code: transient conditions are retryable
/// (COMMON_UNAVAILABLE, COMMON_DEADLINE_EXCEEDED), invalid requests are not.
[[nodiscard]] Error to_error(rd_kafka_resp_err_t code, std::string_view what);
[[nodiscard]] Error to_error(const rd_kafka_error_t* error, std::string_view what);

[[nodiscard]] ListPtr to_list(const Offsets& offsets);
[[nodiscard]] ListPtr to_list(const std::vector<TopicPartition>& partitions);
[[nodiscard]] std::vector<TopicPartition> partitions_of(const rd_kafka_topic_partition_list_t* list);
[[nodiscard]] Offsets offsets_of(const rd_kafka_topic_partition_list_t* list);

/// Wakes a coroutine when a librdkafka queue becomes non-empty: librdkafka writes to an eventfd
/// (rd_kafka_queue_io_event_enable) that Asio watches - no polling thread (ADR-016, item 5).
/// librdkafka signals only the empty -> non-empty transition, so the owner drains the queue
/// before waiting; a periodic wake-up guards against a missed edge.
class QueueEvents {
 public:
  QueueEvents(const asio::any_io_executor& executor, rd_kafka_queue_t* queue);
  QueueEvents(const QueueEvents&) = delete;
  QueueEvents& operator=(const QueueEvents&) = delete;
  QueueEvents(QueueEvents&&) = delete;
  QueueEvents& operator=(QueueEvents&&) = delete;
  ~QueueEvents();

  /// Wait for new events or `timeout`; false once close() was called.
  asio::awaitable<bool> wait(std::chrono::milliseconds timeout);

  /// Wake the waiter and make every later wait() return false.
  void close();

  [[nodiscard]] bool closed() const noexcept { return closed_; }

 private:
  rd_kafka_queue_t* queue_;
  asio::posix::stream_descriptor descriptor_;
  bool closed_ = false;
};

/// Pause between checks of a condition librdkafka does not signal (outgoing queue drained,
/// consumer closed), and the period of the guard wake-up.
inline constexpr std::chrono::milliseconds kServeInterval{100};
inline constexpr std::chrono::milliseconds kFlushInterval{2};

}  // namespace psim::platform::kafka::detail
