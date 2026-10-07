#pragma once

#include <string_view>

namespace psim::service_template {

/// JSON Schema of the `settings` section (ADR-039): x-psim-reload marks run-time settings.
inline constexpr std::string_view kSettingsSchema = R"({
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "interval_ms": {"type": "integer", "minimum": 1, "maximum": 60000, "default": 100, "x-psim-reload": true,
                    "description": "A new message is taken every interval_ms"},
    "work_ms": {"type": "integer", "minimum": 0, "maximum": 60000, "default": 20,
                "description": "Processing time of one message"}
  }
})";

}  // namespace psim::service_template
