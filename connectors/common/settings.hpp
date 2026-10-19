#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "psim/platform/connector/client.hpp"

namespace psim::connectors {
inline std::string setting(const char* name, const std::string& fallback = {}) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): the process never mutates its environment
  const auto* value = std::getenv(name);
  return value == nullptr ? fallback : std::string(value);
}

inline platform::connector::Options options(const std::string& version) {
  return {.endpoint = setting("PSIM_CONNECTOR_ENDPOINT", "connector-gateway:8080"),
          .ca_file = setting("PSIM_CONNECTOR_CA_FILE", "/repo/.cache/event-flow/ca.pem"),
          .certificate_file = setting("PSIM_CONNECTOR_CERTIFICATE_FILE", "/repo/.cache/event-flow/client.pem"),
          .private_key_file = setting("PSIM_CONNECTOR_PRIVATE_KEY_FILE", "/repo/.cache/event-flow/client.key"),
          .buffer_directory = setting("PSIM_CONNECTOR_BUFFER_DIRECTORY"),
          .connector_version = version};
}

inline void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}  // namespace psim::connectors
