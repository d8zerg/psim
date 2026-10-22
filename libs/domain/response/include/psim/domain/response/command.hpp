#pragma once
#include <cstdint>
#include <expected>
#include <string_view>

namespace psim::domain::response {
enum class State : std::uint8_t {
  kRequested = 1,
  kAwaitingApproval,
  kSent,
  kAcknowledged,
  kExecuted,
  kFailed,
  kRejected,
  kTimedOut,
  kCancelled,
  kUnknown
};
enum class Action : std::uint8_t {
  kApprove,
  kReject,
  kCancel,
  kSend,
  kReceipt,
  kSuccess,
  kFailure,
  kExpire,
  kDisconnect
};
enum class Failure : std::uint8_t { kState, kSelfApproval };

struct Command {
  State state = State::kRequested;
  std::string_view requester{};
  std::int64_t deadline = 0;
};

[[nodiscard]] bool terminal(State state);
[[nodiscard]] std::expected<State, Failure> apply(Command command, Action action, std::string_view actor,
                                                  std::int64_t now);
}  // namespace psim::domain::response
