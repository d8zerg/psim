// Configuration schema of every service (ADR-039): runtime sections and the service `settings`.

#include <string>
#include <string_view>

#include "psim/platform/runtime.hpp"

namespace psim::platform::runtime {

namespace {

// Runtime sections. log.level is applied at run time (SIGHUP); everything else needs a restart.
constexpr std::string_view kRuntimeSchemaHead = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "service": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "instance": {"type": "string", "minLength": 1, "description": "Instance name in logs, metrics and traces; default: host name"}
      }
    },
    "admin": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "listen": {"type": "string", "pattern": "^[^:]+:[0-9]{1,5}$", "default": "0.0.0.0:9100",
                   "description": "Address of /health/live, /health/ready, /metrics, /info"}
      }
    },
    "log": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "level": {"type": "string", "enum": ["trace", "debug", "info", "warn", "error"], "default": "info",
                  "x-psim-reload": true}
      }
    },
    "telemetry": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "otlp_endpoint": {"type": "string", "default": "", "description": "OTLP gRPC endpoint of the Collector; empty: no export"},
        "sampling_ratio": {"type": "number", "minimum": 0, "maximum": 1, "default": 0.01}
      }
    },
    "shutdown": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "drain_timeout_ms": {"type": "integer", "minimum": 0, "maximum": 600000, "default": 30000}
      }
    },
    "settings": )";

constexpr std::string_view kRuntimeSchemaTail = R"(
  }
})";

}  // namespace

std::string configuration_schema(std::string_view settings_schema) {
  std::string schema(kRuntimeSchemaHead);
  schema += settings_schema;
  schema += kRuntimeSchemaTail;
  return schema;
}

}  // namespace psim::platform::runtime
