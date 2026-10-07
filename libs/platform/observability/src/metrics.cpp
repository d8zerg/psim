#include "psim/platform/metrics.hpp"

#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include <memory>
#include <string>

namespace psim::platform::observability {

Metrics::Metrics() : registry_(std::make_shared<prometheus::Registry>()) {}

std::string Metrics::expose() const {
  const prometheus::TextSerializer serializer;
  return serializer.Serialize(registry_->Collect());
}

}  // namespace psim::platform::observability
