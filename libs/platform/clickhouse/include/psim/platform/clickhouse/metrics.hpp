#pragma once
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

#include <cstddef>

namespace psim::platform::clickhouse {
/// One instance per process; no tenant, query text or dynamic identifier labels.
class Metrics {
 public:
  explicit Metrics(prometheus::Registry& registry);
  void query(double seconds, bool success);
  void batch(std::size_t rows, double seconds, bool success);
  void lag(double seconds);
  void dead_letter();

 private:
  prometheus::Counter* errors_;
  prometheus::Counter* inserted_rows_;
  prometheus::Counter* dead_letters_;
  prometheus::Histogram* queries_;
  prometheus::Histogram* batches_;
  prometheus::Histogram* batch_rows_;
  prometheus::Gauge* lag_;
};
}  // namespace psim::platform::clickhouse
