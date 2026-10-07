#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "psim/platform/async/clock.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"

namespace psim::platform::kafka {

namespace asio = boost::asio;

/// Delivery classes of ADR-006 the processor implements.
enum class Semantics : std::uint8_t {
  /// ALO: outputs are delivered, then input offsets committed; a crash repeats the batch, so
  /// consumers deduplicate by id (ALO+ID) or the handler writes idempotently (EOS-DB).
  kAtLeastOnce,
  /// EOS-K: outputs and input offsets in one Kafka transaction (read-process-write).
  kExactlyOnce,
};

/// Exponential backoff of a partition after a transient error (ADR-006, item 4).
struct RetryPolicy {
  std::chrono::milliseconds initial{100};
  std::chrono::milliseconds max{30000};
  double multiplier = 2.0;

  /// Delay before attempt `attempt` (1 - the first retry).
  [[nodiscard]] std::chrono::milliseconds delay(std::uint32_t attempt) const noexcept;
};

struct ProcessorOptions {
  /// Pipeline stage: DLQ topic psim.dlq.<stage>.v1 and the metric label.
  std::string stage{};
  Semantics semantics = Semantics::kAtLeastOnce;
  std::size_t max_batch = 500;
  /// How often offsets (ALO) or transactions (EOS-K: 20-50 ms, ADR-006) are committed.
  std::chrono::milliseconds commit_interval{1000};
  RetryPolicy retry{};
  /// Program errors (exceptions) in a row that stop a partition (crosscutting.md, section 4).
  std::uint32_t circuit_breaker = 5;
  /// Bound of flushes, commits and aborts.
  std::chrono::milliseconds timeout{30000};
};

/// Where a handler writes its results: the processor's producer, inside the transaction for EOS-K.
class Output {
 public:
  explicit Output(Producer* producer) noexcept : producer_(producer) {}

  asio::awaitable<Result<void>> send(Record record) { return producer_->enqueue(std::move(record)); }

 private:
  Producer* producer_;
};

/// Logic of a stage. The error of handle() decides what happens to the message: retryable - the
/// partition pauses and the message is retried with backoff (order is kept); not retryable - the
/// message goes to the dead letter topic and processing continues. An exception is a program
/// error: dead letter topic, and the partition stops after ProcessorOptions::circuit_breaker of
/// them in a row. Both pointers stay valid until the returned coroutine completes.
class Handler {
 public:
  Handler() = default;
  Handler(const Handler&) = delete;
  Handler& operator=(const Handler&) = delete;
  Handler(Handler&&) = delete;
  Handler& operator=(Handler&&) = delete;
  virtual ~Handler() = default;

  virtual asio::awaitable<Result<void>> handle(const Message* message, Output* output) = 0;
};

/// Consume - handle - produce loop of a pipeline stage on one shard (ADR-041). The consumer and the
/// producer belong to it while it runs; for EOS-K the producer is transactional.
class Processor final : public RebalanceListener {
 public:
  Processor(Consumer* input, Producer* output, async::Clock* clock, ProcessorOptions options, Dependencies deps = {});
  Processor(const Processor&) = delete;
  Processor& operator=(const Processor&) = delete;
  Processor(Processor&&) = delete;
  Processor& operator=(Processor&&) = delete;
  ~Processor() override;

  /// Process until stop(); the last batch is committed before it returns. An error means the
  /// stage cannot continue (fenced or fatal client): the component should fail.
  asio::awaitable<Result<void>> run(Handler* handler);

  /// Finish the current message and batch, commit, return from run().
  void stop() noexcept { stopping_ = true; }

  void on_assigned(const std::vector<TopicPartition>& partitions) override;
  void on_revoked(const std::vector<TopicPartition>& partitions, bool lost) override;

 private:
  struct Partition {
    std::int64_t committed = -1;  // offset the group has committed (or started from)
    std::int64_t next = -1;       // offset after the last processed message
    std::int64_t transaction_start = -1;
    std::uint32_t attempts = 0;
    std::uint32_t program_errors = 0;
    std::optional<async::SteadyTime> resume_at;
    bool stopped = false;
  };
  enum class Step : std::uint8_t { kNext, kSkipPartition };

  asio::awaitable<Result<Step>> process(Handler* handler, const Message* message, Partition* partition);
  asio::awaitable<Result<void>> dead_letter(const Message* message, Error error);
  void retry_later(const Message& message, Partition& partition, const Error& error);
  void stop_partition(const TopicPartition& id, Partition& partition, std::int64_t next);
  void resume_due();
  asio::awaitable<Result<void>> commit(bool force);
  asio::awaitable<Result<void>> commit_offsets();
  asio::awaitable<Result<void>> commit_transaction();
  void rewind(bool transaction_only);
  [[nodiscard]] Offsets pending_offsets(bool transaction_only) const;
  [[nodiscard]] std::chrono::milliseconds poll_wait() const;
  void forget(const TopicPartition& id);

  Consumer* input_;
  Producer* output_;
  async::Clock* clock_;
  ProcessorOptions options_;
  Dependencies deps_;
  Output sink_;
  std::string dead_letter_topic_;
  std::map<TopicPartition, Partition> partitions_;
  async::SteadyTime last_commit_{};
  async::SteadyTime transaction_started_{};
  bool dirty_ = false;
  bool stopping_ = false;
  bool initialized_ = false;
};

}  // namespace psim::platform::kafka
