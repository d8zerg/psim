#pragma once
#include <optional>
#include <string_view>

namespace psim::domain::processing {
struct Classification {
  std::string_view type;
  bool alarm;
};

inline std::optional<Classification> classify(std::string_view raw_code) {
  if (raw_code == "ALARM") {
    return Classification{"intrusion.zone.alarm", true};
  }
  if (raw_code == "RESTORE") {
    return Classification{"intrusion.zone.restore", false};
  }
  return std::nullopt;
}

inline bool alarm_rule(std::string_view type) {
  return type == "intrusion.zone.alarm";
}
}  // namespace psim::domain::processing
