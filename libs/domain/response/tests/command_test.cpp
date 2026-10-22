#include "psim/domain/response/command.hpp"

#include <gtest/gtest.h>

namespace psim::domain::response {
TEST(Command, ApprovalAndExecution) {
  Command value{State::kAwaitingApproval, "alice", 100};
  EXPECT_EQ(apply(value, Action::kApprove, "alice", 1).error(), Failure::kSelfApproval);
  EXPECT_EQ(*apply(value, Action::kApprove, "bob", 1), State::kRequested);
  value.state = State::kRequested;
  EXPECT_EQ(*apply(value, Action::kSend, "", 2), State::kSent);
  EXPECT_EQ(*apply(value, Action::kCancel, "", 100), State::kTimedOut);
  value.state = State::kSent;
  EXPECT_EQ(*apply(value, Action::kReceipt, "", 3), State::kAcknowledged);
  EXPECT_EQ(*apply(value, Action::kSuccess, "", 4), State::kExecuted);
  EXPECT_EQ(*apply(value, Action::kFailure, "", 4), State::kFailed);
  EXPECT_EQ(*apply(value, Action::kDisconnect, "", 4), State::kUnknown);
  value.state = State::kAcknowledged;
  EXPECT_EQ(*apply(value, Action::kReceipt, "", 4), State::kAcknowledged);
  EXPECT_EQ(*apply(value, Action::kDisconnect, "", 4), State::kUnknown);
  EXPECT_EQ(*apply(value, Action::kSuccess, "", 4), State::kExecuted);
  EXPECT_EQ(*apply(value, Action::kFailure, "", 4), State::kFailed);
}

TEST(Command, ExhaustiveTransitionInvariants) {
  for (int state = 1; state <= 10; ++state) {
    for (int action = 0; action <= 8; ++action) {
      const Command value{static_cast<State>(state), "alice", 100};
      const auto result = apply(value, static_cast<Action>(action), "bob", 1);
      if (terminal(value.state)) {
        EXPECT_FALSE(result);
      }
      if (result && *result == State::kSent) {
        EXPECT_EQ(value.state, State::kRequested);
      }
      if (result && *result == State::kCancelled) {
        EXPECT_TRUE(value.state == State::kRequested || value.state == State::kAwaitingApproval);
      }
      const auto expired = apply(value, static_cast<Action>(action), "bob", 100);
      if (!terminal(value.state)) {
        ASSERT_TRUE(expired);
        EXPECT_EQ(*expired, State::kTimedOut);
      }
    }
  }
}
}  // namespace psim::domain::response
