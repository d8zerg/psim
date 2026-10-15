#include "psim/platform/clickhouse/metrics.hpp"

#include <cstddef>
#include <vector>

namespace psim::platform::clickhouse {
namespace {
prometheus::Counter* counter(prometheus::Registry& registry, const char* name, const char* help) {
  return &prometheus::BuildCounter().Name(name).Help(help).Register(registry).Add({});
}

prometheus::Histogram* histogram(prometheus::Registry& registry, const char* name, const char* help,
                                 const std::vector<double>& bounds) {
  return &prometheus::BuildHistogram().Name(name).Help(help).Register(registry).Add({}, bounds);
}
}  // namespace

Metrics::Metrics(prometheus::Registry& registry)
    : errors_(counter(registry, "psim_clickhouse_errors_total", "Failed ClickHouse operations")),
      inserted_rows_(counter(registry, "psim_clickhouse_inserted_rows_total",
                             "Rows acknowledged by ClickHouse including retries")),
      dead_letters_(counter(registry, "psim_clickhouse_dead_letters_total", "Loader records sent to DLQ")),
      queries_(histogram(registry, "psim_clickhouse_query_seconds", "Native query duration",
                         {0.001, 0.01, 0.1, 0.5, 1, 5, 30})),
      batches_(histogram(registry, "psim_clickhouse_batch_seconds", "Batch insertion duration",
                         {0.001, 0.01, 0.1, 1, 5, 30})),
      batch_rows_(histogram(registry, "psim_clickhouse_batch_rows", "Batch row count", {1, 100, 1000, 10000, 100000})),
      lag_(&prometheus::BuildGauge()
                .Name("psim_clickhouse_ingest_lag_seconds")
                .Help("Age of the newest inserted Kafka record")
                .Register(registry)
                .Add({})) {}

void Metrics::query(double seconds, bool success) {
  queries_->Observe(seconds);
  if (!success) {
    errors_->Increment();
  }
}

void Metrics::batch(std::size_t rows, double seconds, bool success) {
  batches_->Observe(seconds);
  batch_rows_->Observe(static_cast<double>(rows));
  if (success) {
    inserted_rows_->Increment(static_cast<double>(rows));
  } else {
    errors_->Increment();
  }
}

void Metrics::lag(double seconds) {
  lag_->Set(seconds);
}

void Metrics::dead_letter() {
  dead_letters_->Increment();
}
}  // namespace psim::platform::clickhouse
