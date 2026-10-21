#include "psim/domain/incident/model.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>

namespace psim::domain::incident {
bool accepts_signal(GroupWindow window, std::int64_t first_event_at) {
  return window.active && first_event_at <= window.until;
}

int raised_priority(int current, int incoming) {
  return std::min(current, incoming);
}

std::expected<Value, Failure> apply(Value value, const Command& command) {
  const auto reject = [](Failure failure) {
    return std::unexpected(failure);
  };
  if (value.state == State::kClosed) {
    return reject(Failure::kTransition);
  }
  const bool owner = !value.assignee.empty() && value.assignee == command.actor;
  if (command.action == "acknowledge") {
    if (value.state != State::kNew) {
      return reject(Failure::kTransition);
    }
    if (!command.recipient_valid) {
      return reject(Failure::kAssignee);
    }
    value.assignee = command.actor;
    value.state = State::kAcknowledged;
  } else if (command.action == "start-work") {
    if (value.state != State::kAcknowledged) {
      return reject(Failure::kTransition);
    }
    if (!owner) {
      return reject(Failure::kForbidden);
    }
    value.state = State::kInProgress;
  } else if (command.action == "assign" || command.action == "confirm-transfer") {
    if (value.state == State::kResolved) {
      return reject(Failure::kTransition);
    }
    if (!command.recipient_valid || command.assignee.empty()) {
      return reject(Failure::kAssignee);
    }
    if (command.action == "assign" && !command.supervisor && command.assignee != command.actor) {
      return reject(Failure::kForbidden);
    }
    value.assignee = command.assignee;
  } else if (command.action == "set-priority") {
    if (!command.supervisor) {
      return reject(Failure::kForbidden);
    }
    if (command.priority < 1 || command.priority > 4) {
      return reject(Failure::kArgument);
    }
    value.priority = command.priority;
    value.manual = true;
  } else if (command.action == "resolve") {
    if (value.state == State::kResolved) {
      return reject(Failure::kTransition);
    }
    if (command.run_version != value.run_version) {
      return reject(Failure::kRunVersion);
    }
    const bool exception =
        command.supervisor && (command.resolution == Resolution::kDuplicate || command.resolution == Resolution::kTest);
    if (value.blocking && !exception) {
      return reject(Failure::kBlocking);
    }
    if (command.resolution == Resolution::kNone) {
      return reject(Failure::kArgument);
    }
    if (value.state == State::kNew) {
      if (!command.supervisor || command.resolution == Resolution::kConfirmed) {
        return reject(Failure::kForbidden);
      }
    } else if (!owner && !command.supervisor) {
      return reject(Failure::kForbidden);
    }
    value.state = State::kResolved;
    value.resolution = command.resolution;
  } else if (command.action == "reopen") {
    if (value.state != State::kResolved) {
      return reject(Failure::kTransition);
    }
    if (!owner && !command.supervisor) {
      return reject(Failure::kAssignee);
    }
    if (command.supervisor) {
      value.assignee = command.actor;
    }
    if (value.assignee.empty()) {
      return reject(Failure::kAssignee);
    }
    value.state = State::kInProgress;
    value.resolution = Resolution::kNone;
  } else if (command.action == "close") {
    if (value.state != State::kResolved) {
      return reject(Failure::kTransition);
    }
    if (!command.supervisor) {
      return reject(Failure::kForbidden);
    }
    value.state = State::kClosed;
  } else if (command.action == "request-transfer") {
    if (value.state == State::kResolved) {
      return reject(Failure::kTransition);
    }
    if (!owner && !command.supervisor) {
      return reject(Failure::kForbidden);
    }
    if (!command.recipient_valid || command.assignee == value.assignee) {
      return reject(Failure::kAssignee);
    }
  } else if (command.action != "comment" && command.action != "link") {
    return reject(Failure::kArgument);
  }
  return value;
}
}  // namespace psim::domain::incident
