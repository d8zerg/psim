#pragma once

#include <prometheus/counter.h>
#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace psim::platform::kafka {

/// Metric families of the Kafka layer (ADR-041), one instance per process shared by its producers,
/// consumers and processors. Families are thread-safe; series are cached by their users.
class KafkaMetrics {
 public:
  explicit KafkaMetrics(prometheus::Registry& registry);

  prometheus::Counter& produced_messages(std::string_view topic);
  prometheus::Counter& produced_bytes(std::string_view topic);
  prometheus::Counter& delivery_errors(std::string_view topic);
  prometheus::Counter& consumed_messages(std::string_view topic);
  prometheus::Counter& consumed_bytes(std::string_view topic);
  /// Messages between the consumer position and the end of the partition (librdkafka statistics).
  prometheus::Gauge& consumer_lag(std::string_view group, std::string_view topic, std::int32_t partition);
  prometheus::Counter& rebalances(std::string_view group, std::string_view kind);
  prometheus::Counter& retries(std::string_view stage);
  prometheus::Counter& dead_letters(std::string_view stage, std::string_view code);
  prometheus::Counter& transactions(std::string_view result);
  /// Partitions stopped by the circuit breaker after repeated program errors.
  prometheus::Gauge& stopped_partitions(std::string_view stage);

 private:
  prometheus::Family<prometheus::Counter>* produced_messages_;
  prometheus::Family<prometheus::Counter>* produced_bytes_;
  prometheus::Family<prometheus::Counter>* delivery_errors_;
  prometheus::Family<prometheus::Counter>* consumed_messages_;
  prometheus::Family<prometheus::Counter>* consumed_bytes_;
  prometheus::Family<prometheus::Gauge>* consumer_lag_;
  prometheus::Family<prometheus::Counter>* rebalances_;
  prometheus::Family<prometheus::Counter>* retries_;
  prometheus::Family<prometheus::Counter>* dead_letters_;
  prometheus::Family<prometheus::Counter>* transactions_;
  prometheus::Family<prometheus::Gauge>* stopped_partitions_;
};

}  // namespace psim::platform::kafka
