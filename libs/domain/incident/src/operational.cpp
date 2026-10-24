#include "psim/domain/incident/operational.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string_view>

namespace psim::domain::incident {
std::expected<Alarm, AlarmFailure> alarm(Alarm current, std::string_view action, bool supervisor,
                                         std::string_view reason, std::int64_t now, std::int64_t until) {
  if (action == "acknowledge") {
    if (!current.active && current.acknowledged) {
      return std::unexpected(AlarmFailure::kTransition);
    }
    current.acknowledged = true;
  } else if (action == "activate" || action == "return") {
    if (!supervisor) {
      return std::unexpected(AlarmFailure::kForbidden);
    }
    if (reason.empty() || reason.size() > 4096) {
      return std::unexpected(AlarmFailure::kArgument);
    }
    const bool active = action == "activate";
    if (active && !current.active) {
      current.acknowledged = false;
    }
    current.active = active;
  } else if (action == "shelve" || action == "suppress") {
    if (action == "suppress" && !supervisor) {
      return std::unexpected(AlarmFailure::kForbidden);
    }
    if (reason.empty() || reason.size() > 4096 || until <= now || until - now > 86400000) {
      return std::unexpected(AlarmFailure::kArgument);
    }
    (action == "shelve" ? current.shelved_until : current.suppressed_until) = until;
  } else if (action == "unshelve" || action == "unsuppress") {
    if (action == "unsuppress" && !supervisor) {
      return std::unexpected(AlarmFailure::kForbidden);
    }
    if (reason.empty() || reason.size() > 4096) {
      return std::unexpected(AlarmFailure::kArgument);
    }
    (action == "unshelve" ? current.shelved_until : current.suppressed_until) = 0;
  } else {
    return std::unexpected(AlarmFailure::kArgument);
  }
  return current;
}

std::string_view sla_risk(std::int64_t now, std::int64_t due, std::int64_t started) {
  if (due <= started || started <= 0) {
    return "unknown";
  }
  if (now >= due) {
    return "breached";
  }
  return due - now <= std::max<std::int64_t>(1000, (due - started) / 5) ? "at-risk" : "on-time";
}
}  // namespace psim::domain::incident
