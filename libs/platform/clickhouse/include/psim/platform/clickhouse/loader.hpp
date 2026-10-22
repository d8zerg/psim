#pragma once

#include <boost/asio/awaitable.hpp>
#include <clickhouse/block.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/pg/pool.hpp"

namespace psim::platform::clickhouse {
struct Rejected {
  std::size_t index = 0;
  ErrorCode code = ErrorCode::kProcessingPayloadInvalid;
};

struct Converted {
  ::clickhouse::Block block{};
  std::vector<Rejected> rejected{};
  // Valid records outside this projection; offsets advance without rows or DLQ.
  std::vector<std::size_t> ignored{};
};

/// One successful row per input except explicitly rejected or ignored indexes. Populate columns
/// directly from decoded Protobuf values; no intermediate row objects or JSON are required.
using Converter = std::function<Result<Converted>(std::span<const kafka::Message* const>)>;

struct LoaderOptions {
  /// Stable across restarts. Change it when conversion/table semantics change; first drain
  /// the old journal. Never run different converters with the same name and consumer group.
  std::string name;
  std::string table;
  std::string dlq;
  std::size_t max_messages = 10000;
  std::size_t max_bytes = std::size_t{8} * 1024 * 1024;
  Duration flush_after{100};
  Duration kafka_timeout{30000};
};
enum class LoadStage : std::uint8_t { kPrepared, kInserted, kApplied, kCommitted };
using LoadObserver = std::function<void(LoadStage)>;

/// A bounded batch at a time. PostgreSQL stores only unfinished native blocks/DLQ records,
/// fixing boundaries before insertion. The same bytes/token survive crashes and rebalances.
/// A transactionally initialized Kafka producer atomically publishes DLQ and commits offsets.
/// Without rejected records, a regular offset commit follows the durable insert confirmation.
/// Call run_once repeatedly, stop on errors and retry with backoff. Caller owns dependencies.
class Loader {
 public:
  Loader(Pool* destination, pg::Pool* journal, kafka::Consumer* source, kafka::Producer* dlq, LoaderOptions options,
         Converter converter, Metrics* metrics = nullptr, LoadObserver observer = {});
  /// Idempotent control-schema bootstrap; normally run by the deployment identity.
  static asio::awaitable<Result<void>> initialize(pg::Pool* journal);
  asio::awaitable<Result<std::size_t>> run_once();

 private:
  asio::awaitable<Result<void>> deliver(kafka::TopicPartition partition);
  Pool* destination_;
  pg::Pool* journal_;
  kafka::Consumer* source_;
  kafka::Producer* dlq_;
  LoaderOptions options_;
  Converter converter_;
  Metrics* metrics_;
  LoadObserver observer_;
  kafka::Offsets retry_offsets_{};
};
}  // namespace psim::platform::clickhouse
