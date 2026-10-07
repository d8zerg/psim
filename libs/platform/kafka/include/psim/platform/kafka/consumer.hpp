#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"

struct rd_kafka_s;                          // NOLINT(readability-identifier-naming): librdkafka C type
struct rd_kafka_consumer_group_metadata_s;  // NOLINT(readability-identifier-naming): librdkafka C type

namespace psim::platform::kafka {

namespace asio = boost::asio;

struct ConsumerOptions {
  ClientOptions client{};
  std::string group_id{};
  std::vector<std::string> topics{};
  /// Skip messages of aborted transactions and wait for open ones (EOS-K inputs, ADR-006).
  bool read_committed = true;
  /// "consumer" - the KIP-848 protocol (Kafka 4.x, ADR-002); "classic" - cooperative-sticky.
  std::string group_protocol = "consumer";
  /// Where a group without a committed offset starts.
  std::string auto_offset_reset = "earliest";
};

/// Called inside Consumer::poll(), so between batches: nothing of the consumer is in progress.
class RebalanceListener {
 public:
  RebalanceListener() = default;
  RebalanceListener(const RebalanceListener&) = delete;
  RebalanceListener& operator=(const RebalanceListener&) = delete;
  RebalanceListener(RebalanceListener&&) = delete;
  RebalanceListener& operator=(RebalanceListener&&) = delete;
  virtual ~RebalanceListener() = default;

  virtual void on_assigned(const std::vector<TopicPartition>& partitions) = 0;
  /// Before the partitions leave: the last moment to commit their offsets. `lost` - they already
  /// belong to another member (session timeout); nothing can be committed for them.
  virtual void on_revoked(const std::vector<TopicPartition>& partitions, bool lost) = 0;
};

struct GroupMetadataDeleter {
  void operator()(rd_kafka_consumer_group_metadata_s* metadata) const noexcept;
};

using GroupMetadata = std::unique_ptr<rd_kafka_consumer_group_metadata_s, GroupMetadataDeleter>;

/// Group consumer with manual offsets: nothing is committed automatically (ADR-041). Bound to the
/// executor it was created on (a shard). Rebalances arrive through the listener inside poll().
class Consumer {
 public:
  using Duration = std::chrono::milliseconds;

  [[nodiscard]] static Result<std::unique_ptr<Consumer>> create(const asio::any_io_executor& executor,
                                                                ConsumerOptions options, Dependencies deps = {});

  Consumer(const Consumer&) = delete;
  Consumer& operator=(const Consumer&) = delete;
  Consumer(Consumer&&) = delete;
  Consumer& operator=(Consumer&&) = delete;
  /// Without close() the member leaves the group only by session timeout (as after a crash).
  ~Consumer();

  void set_listener(RebalanceListener* listener) noexcept;

  /// Up to `max` messages of the assigned partitions; waits up to `wait` for the first one and
  /// returns an empty batch after it. Serves rebalances and statistics.
  asio::awaitable<Result<std::vector<Message>>> poll(std::size_t max, Duration wait);

  /// Commit offsets synchronously in the blocking pool (Dependencies::blocking required).
  asio::awaitable<Result<void>> commit(Offsets offsets);
  /// Blocking commit for callbacks that cannot suspend (a rebalance).
  Result<void> commit_now(const Offsets& offsets);

  /// Committed offsets of the group (blocking pool); partitions without one are left out.
  asio::awaitable<Result<Offsets>> committed(std::vector<TopicPartition> partitions, Duration timeout);

  /// Continue reading the partitions from the given offsets.
  Result<void> seek(const Offsets& offsets);
  Result<void> pause(const std::vector<TopicPartition>& partitions);
  Result<void> resume(const std::vector<TopicPartition>& partitions);

  [[nodiscard]] const std::set<TopicPartition>& assignment() const noexcept;
  [[nodiscard]] const std::string& group() const noexcept;

  /// For send_offsets_to_transaction: the group generation fences zombie producers (KIP-447).
  [[nodiscard]] GroupMetadata group_metadata() const;

  /// Leave the group: revokes the partitions through the listener, then stops.
  asio::awaitable<void> close();

  [[nodiscard]] rd_kafka_s* handle() const noexcept;

 private:
  struct State;
  explicit Consumer(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
};

}  // namespace psim::platform::kafka
