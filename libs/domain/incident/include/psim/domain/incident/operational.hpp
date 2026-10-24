#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace psim::domain::incident {
struct Alarm {
  bool active = false;
  bool acknowledged = false;
  std::int64_t shelved_until = 0;
  std::int64_t suppressed_until = 0;
};

enum class AlarmFailure : std::uint8_t { kArgument, kForbidden, kTransition };

/// Alarm acknowledgement and process condition are independent of incident resolution.
[[nodiscard]] std::expected<Alarm, AlarmFailure> alarm(Alarm current, std::string_view action, bool supervisor,
                                                       std::string_view reason, std::int64_t now,
                                                       std::int64_t until = 0);
/// Risk refers to the pinned deadline; absence of a target is explicitly unknown.
[[nodiscard]] std::string_view sla_risk(std::int64_t now, std::int64_t due, std::int64_t started);
}  // namespace psim::domain::incident
