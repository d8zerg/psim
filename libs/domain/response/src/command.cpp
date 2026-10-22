#include "psim/domain/response/command.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace psim::domain::response {
bool terminal(State state) {
  return state >= State::kExecuted;
}

std::expected<State, Failure> apply(Command command, Action action, std::string_view actor, std::int64_t now) {
  const auto state = command.state;
  if (terminal(state)) {
    return std::unexpected(Failure::kState);
  }
  if (action == Action::kExpire || now >= command.deadline) {
    return State::kTimedOut;
  }
  switch (action) {
    case Action::kApprove:
      if (state != State::kAwaitingApproval) {
        break;
      }
      if (actor == command.requester) {
        return std::unexpected(Failure::kSelfApproval);
      }
      return State::kRequested;
    case Action::kReject:
      if (state == State::kAwaitingApproval) {
        return State::kRejected;
      }
      break;
    case Action::kCancel:
      if (state == State::kRequested || state == State::kAwaitingApproval) {
        return State::kCancelled;
      }
      break;
    case Action::kSend:
      if (state == State::kRequested) {
        return State::kSent;
      }
      break;
    case Action::kReceipt:
      if (state == State::kSent || state == State::kAcknowledged) {
        return State::kAcknowledged;
      }
      break;
    case Action::kSuccess:
    case Action::kFailure:
      if (state == State::kSent || state == State::kAcknowledged
          || (state == State::kRequested && action == Action::kFailure)) {
        return action == Action::kSuccess ? State::kExecuted : State::kFailed;
      }
      break;
    case Action::kDisconnect:
      if (state == State::kSent || state == State::kAcknowledged) {
        return State::kUnknown;
      }
      break;
    case Action::kExpire:
      break;
  }
  return std::unexpected(Failure::kState);
}
}  // namespace psim::domain::response
