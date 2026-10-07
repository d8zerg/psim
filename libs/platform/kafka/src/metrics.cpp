#include "psim/platform/kafka/metrics.hpp"

#include <prometheus/counter.h>
#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace psim::platform::kafka {

namespace {

prometheus::Family<prometheus::Counter>* counter(prometheus::Registry& registry, const std::string& name,
                                                 const std::string& help) {
  return &prometheus::BuildCounter().Name(name).Help(help).Register(registry);
}

prometheus::Family<prometheus::Gauge>* gauge(prometheus::Registry& registry, const std::string& name,
                                             const std::string& help) {
  return &prometheus::BuildGauge().Name(name).Help(help).Register(registry);
}

}  // namespace

KafkaMetrics::KafkaMetrics(prometheus::Registry& registry)
    : produced_messages_(counter(registry, "psim_kafka_produced_messages_total", "Messages acknowledged by Kafka")),
      produced_bytes_(counter(registry, "psim_kafka_produced_bytes_total", "Value bytes acknowledged by Kafka")),
      delivery_errors_(counter(registry, "psim_kafka_delivery_errors_total", "Messages Kafka did not acknowledge")),
      consumed_messages_(counter(registry, "psim_kafka_consumed_messages_total", "Messages read from Kafka")),
      consumed_bytes_(counter(registry, "psim_kafka_consumed_bytes_total", "Value bytes read from Kafka")),
      consumer_lag_(gauge(registry, "psim_kafka_consumer_lag", "Messages behind the end of the partition")),
      rebalances_(counter(registry, "psim_kafka_rebalances_total", "Partitions assigned or revoked by the group")),
      retries_(counter(registry, "psim_kafka_retries_total", "Messages retried after a transient error")),
      dead_letters_(counter(registry, "psim_kafka_dead_letters_total", "Messages sent to the dead letter topic")),
      transactions_(counter(registry, "psim_kafka_transactions_total", "Kafka transactions by result")),
      stopped_partitions_(
          gauge(registry, "psim_kafka_stopped_partitions", "Partitions stopped after repeated program errors")) {}

prometheus::Counter& KafkaMetrics::produced_messages(std::string_view topic) {
  return produced_messages_->Add({{"topic", std::string(topic)}});
}

prometheus::Counter& KafkaMetrics::produced_bytes(std::string_view topic) {
  return produced_bytes_->Add({{"topic", std::string(topic)}});
}

prometheus::Counter& KafkaMetrics::delivery_errors(std::string_view topic) {
  return delivery_errors_->Add({{"topic", std::string(topic)}});
}

prometheus::Counter& KafkaMetrics::consumed_messages(std::string_view topic) {
  return consumed_messages_->Add({{"topic", std::string(topic)}});
}

prometheus::Counter& KafkaMetrics::consumed_bytes(std::string_view topic) {
  return consumed_bytes_->Add({{"topic", std::string(topic)}});
}

prometheus::Gauge& KafkaMetrics::consumer_lag(std::string_view group, std::string_view topic, std::int32_t partition) {
  return consumer_lag_->Add(
      {{"group", std::string(group)}, {"topic", std::string(topic)}, {"partition", std::to_string(partition)}});
}

prometheus::Counter& KafkaMetrics::rebalances(std::string_view group, std::string_view kind) {
  return rebalances_->Add({{"group", std::string(group)}, {"kind", std::string(kind)}});
}

prometheus::Counter& KafkaMetrics::retries(std::string_view stage) {
  return retries_->Add({{"stage", std::string(stage)}});
}

prometheus::Counter& KafkaMetrics::dead_letters(std::string_view stage, std::string_view code) {
  return dead_letters_->Add({{"stage", std::string(stage)}, {"code", std::string(code)}});
}

prometheus::Counter& KafkaMetrics::transactions(std::string_view result) {
  return transactions_->Add({{"result", std::string(result)}});
}

prometheus::Gauge& KafkaMetrics::stopped_partitions(std::string_view stage) {
  return stopped_partitions_->Add({{"stage", std::string(stage)}});
}

}  // namespace psim::platform::kafka
