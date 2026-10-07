#include "detail.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/posix/descriptor_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <librdkafka/rdkafka.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"

namespace psim::platform::kafka::detail {

namespace {

constexpr std::size_t kErrorText = 512;

bool timeout(rd_kafka_resp_err_t code) {
  switch (code) {
    case RD_KAFKA_RESP_ERR__TIMED_OUT:
    case RD_KAFKA_RESP_ERR__MSG_TIMED_OUT:
    case RD_KAFKA_RESP_ERR__TIMED_OUT_QUEUE:
    case RD_KAFKA_RESP_ERR_REQUEST_TIMED_OUT:
      return true;
    default:
      return false;
  }
}

bool transient(rd_kafka_resp_err_t code) {
  switch (code) {
    case RD_KAFKA_RESP_ERR__TRANSPORT:
    case RD_KAFKA_RESP_ERR__ALL_BROKERS_DOWN:
    case RD_KAFKA_RESP_ERR__RESOLVE:
    case RD_KAFKA_RESP_ERR__QUEUE_FULL:
    case RD_KAFKA_RESP_ERR__WAIT_COORD:
    case RD_KAFKA_RESP_ERR__STATE:
    case RD_KAFKA_RESP_ERR__PREV_IN_PROGRESS:
    case RD_KAFKA_RESP_ERR_LEADER_NOT_AVAILABLE:
    case RD_KAFKA_RESP_ERR_NOT_LEADER_FOR_PARTITION:
    case RD_KAFKA_RESP_ERR_BROKER_NOT_AVAILABLE:
    case RD_KAFKA_RESP_ERR_NETWORK_EXCEPTION:
    case RD_KAFKA_RESP_ERR_COORDINATOR_LOAD_IN_PROGRESS:
    case RD_KAFKA_RESP_ERR_COORDINATOR_NOT_AVAILABLE:
    case RD_KAFKA_RESP_ERR_NOT_COORDINATOR:
    case RD_KAFKA_RESP_ERR_NOT_ENOUGH_REPLICAS:
    case RD_KAFKA_RESP_ERR_NOT_ENOUGH_REPLICAS_AFTER_APPEND:
    case RD_KAFKA_RESP_ERR_REBALANCE_IN_PROGRESS:
    case RD_KAFKA_RESP_ERR_KAFKA_STORAGE_ERROR:
    case RD_KAFKA_RESP_ERR_CONCURRENT_TRANSACTIONS:
    case RD_KAFKA_RESP_ERR_UNKNOWN_TOPIC_OR_PART:  // also while a new topic propagates
      return true;
    default:
      return false;
  }
}

bool invalid(rd_kafka_resp_err_t code) {
  switch (code) {
    case RD_KAFKA_RESP_ERR__INVALID_ARG:
    case RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC:
    case RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION:
    case RD_KAFKA_RESP_ERR__NOT_IMPLEMENTED:
    case RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE:
    case RD_KAFKA_RESP_ERR_INVALID_MSG:
    case RD_KAFKA_RESP_ERR_INVALID_CONFIG:
      return true;
    default:
      return false;
  }
}

ErrorCode catalog_code(rd_kafka_resp_err_t code, bool retriable) {
  if (timeout(code)) {
    return ErrorCode::kCommonDeadlineExceeded;
  }
  if (retriable || transient(code)) {
    return ErrorCode::kCommonUnavailable;
  }
  if (invalid(code)) {
    return ErrorCode::kCommonInvalidArgument;
  }
  return ErrorCode::kCommonInternal;
}

Error make_error(rd_kafka_resp_err_t code, bool retriable, std::string_view what, std::string_view text) {
  std::string message(what);
  message += ": ";
  message += rd_kafka_err2name(code);
  if (!text.empty()) {
    message += " (";
    message += text;
    message += ")";
  }
  return {catalog_code(code, retriable), std::move(message)};
}

}  // namespace

Result<ConfPtr> make_conf(const ClientOptions& options, const Properties& defaults, const Properties& fixed) {
  ConfPtr conf(rd_kafka_conf_new());
  std::array<char, kErrorText> text{};
  std::string problems;
  const auto set = [&](const std::string& name, const std::string& value) {
    if (rd_kafka_conf_set(conf.get(), name.c_str(), value.c_str(), text.data(), text.size()) != RD_KAFKA_CONF_OK) {
      problems += (problems.empty() ? "" : "; ") + std::string(text.data());
    }
  };
  if (options.bootstrap.empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "kafka: bootstrap servers are not set");
  }
  set("bootstrap.servers", options.bootstrap);
  if (!options.client_id.empty()) {
    set("client.id", options.client_id);
  }
  for (const auto& [name, value] : defaults) {
    set(name, value);
  }
  for (const auto& [name, value] : options.properties) {
    if (const auto it = fixed.find(name); it != fixed.end() && it->second != value) {
      problems += (problems.empty() ? "" : "; ") + name + " is fixed to " + it->second + " by the delivery semantics";
      continue;
    }
    set(name, value);
  }
  for (const auto& [name, value] : fixed) {
    set(name, value);
  }
  if (!problems.empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "kafka configuration: " + problems);
  }
  return conf;
}

Error to_error(rd_kafka_resp_err_t code, std::string_view what) {
  return make_error(code, false, what, {});
}

Error to_error(const rd_kafka_error_t* error, std::string_view what) {
  return make_error(rd_kafka_error_code(error), rd_kafka_error_is_retriable(error) != 0, what,
                    rd_kafka_error_string(error));
}

ListPtr to_list(const Offsets& offsets) {
  ListPtr list(rd_kafka_topic_partition_list_new(static_cast<int>(offsets.size())));
  for (const auto& [partition, offset] : offsets) {
    rd_kafka_topic_partition_list_add(list.get(), partition.topic.c_str(), partition.partition)->offset = offset;
  }
  return list;
}

ListPtr to_list(const std::vector<TopicPartition>& partitions) {
  ListPtr list(rd_kafka_topic_partition_list_new(static_cast<int>(partitions.size())));
  for (const auto& partition : partitions) {
    rd_kafka_topic_partition_list_add(list.get(), partition.topic.c_str(), partition.partition);
  }
  return list;
}

std::vector<TopicPartition> partitions_of(const rd_kafka_topic_partition_list_t* list) {
  std::vector<TopicPartition> partitions;
  if (list == nullptr) {
    return partitions;
  }
  partitions.reserve(static_cast<std::size_t>(list->cnt));
  for (int i = 0; i < list->cnt; ++i) {
    const auto& element = list->elems[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): C list
    partitions.push_back({.topic = element.topic, .partition = element.partition});
  }
  return partitions;
}

Offsets offsets_of(const rd_kafka_topic_partition_list_t* list) {
  Offsets offsets;
  for (int i = 0; list != nullptr && i < list->cnt; ++i) {
    const auto& element = list->elems[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): C list
    offsets[{.topic = element.topic, .partition = element.partition}] = element.offset;
  }
  return offsets;
}

QueueEvents::QueueEvents(const asio::any_io_executor& executor, rd_kafka_queue_t* queue)
    : queue_(queue), descriptor_(executor) {
  const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (fd < 0) {
    throw std::system_error(errno, std::generic_category(), "eventfd");
  }
  descriptor_.assign(fd);
  // An eventfd takes 8-byte increments; librdkafka copies the payload.
  static constexpr std::uint64_t kIncrement = 1;
  rd_kafka_queue_io_event_enable(queue_, fd, &kIncrement, sizeof kIncrement);
}

QueueEvents::~QueueEvents() {
  rd_kafka_queue_io_event_enable(queue_, -1, nullptr, 0);
}

asio::awaitable<bool> QueueEvents::wait(std::chrono::milliseconds timeout) {
  using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator||
  if (closed_) {
    co_return false;
  }
  asio::steady_timer timer(co_await asio::this_coro::executor, timeout);
  std::ignore =
      co_await (descriptor_.async_wait(asio::posix::descriptor_base::wait_read, asio::as_tuple(asio::use_awaitable))
                || timer.async_wait(asio::as_tuple(asio::use_awaitable)));
  if (closed_) {
    co_return false;
  }
  std::uint64_t count = 0;
  // Reset the counter; EAGAIN (nothing written, the timer fired) is expected.
  std::ignore = ::read(descriptor_.native_handle(), &count, sizeof count);
  co_return true;
}

void QueueEvents::close() {
  closed_ = true;
  boost::system::error_code ignored;
  std::ignore = descriptor_.cancel(ignored);
}

}  // namespace psim::platform::kafka::detail
