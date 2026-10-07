#pragma once

#include <prometheus/registry.h>

#include <memory>
#include <string>

namespace psim::platform::observability {

/// Metrics of a process, exposed at /metrics in the Prometheus text format (crosscutting.md 6.2).
/// Names follow psim_<subsystem>_<metric>_<unit>; components register their families on registry().
class Metrics {
 public:
  Metrics();

  [[nodiscard]] prometheus::Registry& registry() noexcept { return *registry_; }

  /// Current values in the Prometheus text exposition format 0.0.4.
  [[nodiscard]] std::string expose() const;

 private:
  std::shared_ptr<prometheus::Registry> registry_;
};

}  // namespace psim::platform::observability
