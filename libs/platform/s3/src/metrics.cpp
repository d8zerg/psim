#include "psim/platform/s3/metrics.hpp"

#include <cstddef>
#include <vector>

namespace psim::platform::s3 {
Metrics::Metrics(prometheus::Registry& registry)
    : duration_(&prometheus::BuildHistogram()
                     .Name("psim_s3_operation_seconds")
                     .Help("S3 operation including admission and reconciliation")
                     .Register(registry)
                     .Add({}, std::vector<double>{0.01, 0.1, 0.5, 1, 5, 10, 30})),
      bytes_(&prometheus::BuildCounter()
                  .Name("psim_s3_transferred_bytes_total")
                  .Help("Successfully verified S3 object bytes")
                  .Register(registry)
                  .Add({})),
      errors_(&prometheus::BuildCounter()
                   .Name("psim_s3_errors_total")
                   .Help("Failed S3 operations")
                   .Register(registry)
                   .Add({})),
      retries_(&prometheus::BuildCounter()
                    .Name("psim_s3_retries_total")
                    .Help("Explicit safe S3 retries")
                    .Register(registry)
                    .Add({})) {}

void Metrics::completed(double seconds, std::size_t bytes, bool success) {
  duration_->Observe(seconds);
  if (success) {
    bytes_->Increment(static_cast<double>(bytes));
  } else {
    errors_->Increment();
  }
}

void Metrics::transferred(std::size_t bytes) {
  bytes_->Increment(static_cast<double>(bytes));
}

void Metrics::retried() {
  retries_->Increment();
}
}  // namespace psim::platform::s3
