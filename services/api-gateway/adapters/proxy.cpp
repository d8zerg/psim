#include "proxy.hpp"

#include <array>

namespace psim::api_gateway {
void request_fields(const http::fields& input, http::fields& output) {
  for (const auto* name : std::array{"Authorization", "Content-Type", "Idempotency-Key", "If-Match", "If-None-Match",
                                     "Accept", "traceparent", "tracestate", "User-Agent"}) {
    const auto value = input[name];
    if (!value.empty()) {
      output.set(name, value);
    }
  }
}

void response_fields(const http::fields& input, http::fields& output) {
  for (const auto* name :
       std::array{"Content-Type", "ETag", "Content-Disposition", "Retry-After", "WWW-Authenticate"}) {
    const auto value = input[name];
    if (!value.empty()) {
      output.set(name, value);
    }
  }
}
}  // namespace psim::api_gateway
