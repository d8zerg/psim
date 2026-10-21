#pragma once
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace psim::domain::incident {
enum class State : std::uint8_t { kNew = 1, kAcknowledged, kInProgress, kResolved, kClosed };
enum class Resolution : std::uint8_t { kNone, kConfirmed, kFalseAlarm, kTest, kDuplicate };
enum class Failure : std::uint8_t { kTransition, kAssignee, kForbidden, kBlocking, kRunVersion, kArgument };

struct Value {
  State state = State::kNew;
  Resolution resolution = Resolution::kNone;
  std::string assignee{};
  int priority = 4;
  bool manual = false;
  bool blocking = false;
  std::uint64_t run_version = 0;
};

struct Command {
  std::string_view action;
  std::string_view actor;
  bool supervisor = false;
  std::string_view assignee{};
  bool recipient_valid = false;
  Resolution resolution = Resolution::kNone;
  std::uint64_t run_version = 0;
  int priority = 0;
};

struct GroupWindow {
  bool active = false;
  std::int64_t until = 0;
};

[[nodiscard]] bool accepts_signal(GroupWindow window, std::int64_t first_event_at);
/// All inputs are values; no clocks, identity providers or storage in the aggregate.
[[nodiscard]] std::expected<Value, Failure> apply(Value value, const Command& command);
[[nodiscard]] int raised_priority(int current, int incoming);
}  // namespace psim::domain::incident
