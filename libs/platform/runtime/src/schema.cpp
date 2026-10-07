// Configuration schema of every service (ADR-039): runtime sections and the service `settings`.

#include <string>
#include <string_view>

#include "psim/platform/runtime.hpp"

namespace psim::platform::runtime {

namespace {

// Runtime sections. log.level is applied at run time (SIGHUP); everything else needs a restart.
constexpr std::string_view kRuntimeSchemaHead = R"json({
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
    "runtime": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "shards": {"type": "integer", "minimum": 0, "maximum": 1024, "default": 0,
                   "description": "Threads of the data path; 0: the CPUs available to the process (ADR-016)"},
        "blocking_threads": {"type": "integer", "minimum": 1, "maximum": 256, "default": 4},
        "blocking_tasks": {"type": "integer", "minimum": 1, "maximum": 100000, "default": 64,
                           "description": "Blocking calls running or waiting in the pool; callers beyond wait"}
      }
    },
    "shutdown": {
      "type": "object", "additionalProperties": false,
      "properties": {
        "drain_timeout_ms": {"type": "integer", "minimum": 0, "maximum": 600000, "default": 30000}
      }
    },
    "settings": )json";

constexpr std::string_view kRuntimeSchemaTail = R"json(
  }
})json";

}  // namespace

std::string configuration_schema(std::string_view settings_schema) {
  std::string schema(kRuntimeSchemaHead);
  schema += settings_schema;
  schema += kRuntimeSchemaTail;
  return schema;
}

}  // namespace psim::platform::runtime
