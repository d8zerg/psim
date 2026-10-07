#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <memory>
#include <string>

#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"

struct rd_kafka_s;  // NOLINT(readability-identifier-naming): librdkafka C type

namespace psim::platform::kafka {

namespace asio = boost::asio;

class Consumer;

struct ProducerOptions {
  ClientOptions client{};
  /// Transactional producer (EOS-K, ADR-006): a stable id unique among the running instances,
  /// "<service>-<instance>-<shard>" (ADR-041). Empty: idempotent producer without transactions.
  std::string transactional_id{};
  /// How long librdkafka retries a message before reporting a delivery error.
  std::chrono::milliseconds delivery_timeout{120000};
};

/// Idempotent producer: acks=all, no duplicates or reordering on retries (ADR-041). Bound to the
/// executor it was created on (a shard): every method and the destructor run there. Delivery
/// reports are served by a coroutine that wakes on the librdkafka queue event - no polling thread.
class Producer {
 public:
  using Duration = std::chrono::milliseconds;

  [[nodiscard]] static Result<std::unique_ptr<Producer>> create(const asio::any_io_executor& executor,
                                                                ProducerOptions options, Dependencies deps = {});

  Producer(const Producer&) = delete;
  Producer& operator=(const Producer&) = delete;
  Producer(Producer&&) = delete;
  Producer& operator=(Producer&&) = delete;
  /// Messages not delivered by now are dropped; flush() first to keep them.
  ~Producer();

  /// Hand a message to librdkafka. Waits only while its local queue is full (backpressure); the
  /// outcome of the delivery is reported by flush() or, in a transaction, by its commit.
  asio::awaitable<Result<void>> enqueue(Record record);

  /// Hand a message over and wait until all in-sync replicas have it.
  asio::awaitable<Result<Delivery>> send(Record record);

  /// Wait until every enqueued message is delivered; the first delivery error since the last
  /// flush fails it.
  asio::awaitable<Result<void>> flush(Duration timeout);

  /// Blocking flush for callbacks that cannot suspend (a rebalance).
  Result<void> flush_now(Duration timeout);

  // --- Transactions (transactional_id set; Dependencies::blocking required) ---------------------

  /// Register the transactional id and fence older producers with it; once, before begin.
  asio::awaitable<Result<void>> init_transactions(Duration timeout);

  Result<void> begin_transaction();

  /// Atomically commit the messages of the transaction and the consumer offsets `offsets` of
  /// `source` (read-process-write). On failure the transaction must be aborted, unless fatal().
  asio::awaitable<Result<void>> commit_transaction(const Consumer* source, Offsets offsets, Duration timeout);
  Result<void> commit_transaction_now(const Consumer* source, const Offsets& offsets, Duration timeout);

  asio::awaitable<Result<void>> abort_transaction(Duration timeout);
  Result<void> abort_transaction_now(Duration timeout);

  [[nodiscard]] bool in_transaction() const noexcept;

  /// The producer cannot continue (fenced by a newer instance, fatal idempotence error) and must
  /// be recreated; its open transaction is lost.
  [[nodiscard]] bool fatal() const noexcept;

  [[nodiscard]] rd_kafka_s* handle() const noexcept;

 private:
  struct State;
  explicit Producer(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
};

}  // namespace psim::platform::kafka
