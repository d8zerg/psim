#pragma once
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

#include <cstddef>

namespace psim::platform::s3 {
class Metrics {
 public:
  explicit Metrics(prometheus::Registry& registry);
  void completed(double seconds, std::size_t bytes, bool success);
  void retried();
  void transferred(std::size_t bytes);

 private:
  prometheus::Histogram* duration_;
  prometheus::Counter* bytes_;
  prometheus::Counter* errors_;
  prometheus::Counter* retries_;
};
}  // namespace psim::platform::s3
