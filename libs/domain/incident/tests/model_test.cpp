#include "psim/domain/incident/model.hpp"

#include <gtest/gtest.h>

#include <array>
#include <random>
#include <string_view>

namespace psim::domain::incident {
TEST(Incident, GroupWindowIncludesBoundaryAndExcludesExpiredOrInactiveSlot) {
  EXPECT_TRUE(accepts_signal({.active = true, .until = 100}, 100));
  EXPECT_TRUE(accepts_signal({.active = true, .until = 100}, 99));
  EXPECT_FALSE(accepts_signal({.active = true, .until = 100}, 101));
  EXPECT_FALSE(accepts_signal({.active = false, .until = 100}, 99));
}

TEST(Incident, ResolvesAndReopensWithResponsibilityAndBlocksClosedChanges) {
  const Value value;
  auto accepted = apply(value, {.action = "acknowledge", .actor = "alice", .recipient_valid = true});
  ASSERT_TRUE(accepted);
  EXPECT_EQ(accepted->assignee, "alice");
  EXPECT_EQ(accepted->state, State::kAcknowledged);
  auto working = apply(*accepted, {.action = "start-work", .actor = "alice"});
  ASSERT_TRUE(working);
  auto resolved = apply(*working, {.action = "resolve", .actor = "alice", .resolution = Resolution::kConfirmed});
  ASSERT_TRUE(resolved);
  auto reopened = apply(*resolved, {.action = "reopen", .actor = "alice"});
  ASSERT_TRUE(reopened);
  EXPECT_EQ(reopened->resolution, Resolution::kNone);
  auto again = apply(*reopened, {.action = "resolve", .actor = "alice", .resolution = Resolution::kFalseAlarm});
  ASSERT_TRUE(again);
  auto closed = apply(*again, {.action = "close", .actor = "chief", .supervisor = true});
  ASSERT_TRUE(closed);
  for (const auto* action : {"acknowledge", "start-work", "resolve", "reopen", "close", "comment", "link", "assign"}) {
    EXPECT_FALSE(apply(*closed, {.action = action, .actor = "chief", .supervisor = true, .recipient_valid = true}));
  }
}

TEST(Incident, SupervisorReopensUnassignedMassResolutionWithResponsibility) {
  const auto resolved =
      apply({}, {.action = "resolve", .actor = "chief", .supervisor = true, .resolution = Resolution::kFalseAlarm});
  ASSERT_TRUE(resolved);
  const auto reopened = apply(*resolved, {.action = "reopen", .actor = "chief", .supervisor = true});
  ASSERT_TRUE(reopened);
  EXPECT_EQ(reopened->assignee, "chief");
  EXPECT_EQ(reopened->state, State::kInProgress);
}

TEST(Incident, BlocksUnseenAndMandatoryResponseSteps) {
  const Value value{.state = State::kInProgress, .assignee = "alice", .blocking = true, .run_version = 5};
  EXPECT_EQ(
      apply(value, {.action = "resolve", .actor = "alice", .resolution = Resolution::kConfirmed, .run_version = 4})
          .error(),
      Failure::kRunVersion);
  EXPECT_EQ(
      apply(value, {.action = "resolve", .actor = "alice", .resolution = Resolution::kConfirmed, .run_version = 5})
          .error(),
      Failure::kBlocking);
  EXPECT_FALSE(apply(value, {.action = "resolve",
                             .actor = "chief",
                             .supervisor = true,
                             .resolution = Resolution::kFalseAlarm,
                             .run_version = 5}));
  for (auto resolution : {Resolution::kTest, Resolution::kDuplicate}) {
    EXPECT_TRUE(
        apply(value,
              {.action = "resolve", .actor = "chief", .supervisor = true, .resolution = resolution, .run_version = 5}));
  }
}

TEST(Incident, NewResolutionRequiresSupervisorAndNonConfirmedOutcome) {
  for (auto resolution : {Resolution::kConfirmed, Resolution::kFalseAlarm, Resolution::kTest, Resolution::kDuplicate}) {
    EXPECT_FALSE(apply({}, {.action = "resolve", .actor = "alice", .resolution = resolution}));
    EXPECT_EQ(
        apply({}, {.action = "resolve", .actor = "chief", .supervisor = true, .resolution = resolution}).has_value(),
        resolution != Resolution::kConfirmed);
  }
  EXPECT_FALSE(apply({}, {.action = "resolve", .actor = "chief", .supervisor = true}));
}

TEST(Incident, AssignmentAndTransferPreserveResponseAndPriority) {
  const Value value{.state = State::kInProgress,
                    .assignee = "alice",
                    .priority = 1,
                    .manual = true,
                    .blocking = true,
                    .run_version = 7};
  EXPECT_FALSE(apply(value, {.action = "assign", .actor = "alice", .assignee = "bob", .recipient_valid = true}));
  auto pending =
      apply(value, {.action = "request-transfer", .actor = "alice", .assignee = "bob", .recipient_valid = true});
  ASSERT_TRUE(pending);
  EXPECT_EQ(pending->assignee, "alice");
  auto transferred =
      apply(*pending, {.action = "confirm-transfer", .actor = "bob", .assignee = "bob", .recipient_valid = true});
  ASSERT_TRUE(transferred);
  EXPECT_EQ(transferred->assignee, "bob");
  EXPECT_EQ(transferred->run_version, 7);
  EXPECT_TRUE(transferred->blocking);
  EXPECT_EQ(transferred->priority, 1);
  EXPECT_FALSE(
      apply(value, {.action = "request-transfer", .actor = "mallory", .assignee = "bob", .recipient_valid = true}));
  EXPECT_FALSE(
      apply(value, {.action = "request-transfer", .actor = "alice", .assignee = "alice", .recipient_valid = true}));
  EXPECT_FALSE(apply(value, {.action = "assign", .actor = "chief", .supervisor = true, .assignee = "bob"}));
  EXPECT_TRUE(apply(
      value, {.action = "assign", .actor = "chief", .supervisor = true, .assignee = "bob", .recipient_valid = true}));
}

TEST(Incident, PriorityCannotBeLoweredByIncomingSignals) {
  for (int current = 1; current <= 4; ++current) {
    for (int incoming = 1; incoming <= 4; ++incoming) {
      const auto raised = raised_priority(current, incoming);
      EXPECT_LE(raised, current);
      EXPECT_LE(raised, incoming);
      EXPECT_GE(raised, 1);
    }
  }
  EXPECT_FALSE(apply({}, {.action = "set-priority", .actor = "alice", .priority = 2}));
  EXPECT_FALSE(apply({}, {.action = "set-priority", .actor = "chief", .supervisor = true, .priority = 0}));
  EXPECT_FALSE(apply({}, {.action = "set-priority", .actor = "chief", .supervisor = true, .priority = 5}));
  auto changed = apply({}, {.action = "set-priority", .actor = "chief", .supervisor = true, .priority = 2});
  ASSERT_TRUE(changed);
  EXPECT_TRUE(changed->manual);
}

TEST(Incident, RandomCommandSequencesPreserveAggregateInvariants) {
  constexpr std::array<std::string_view, 14> kActions{
      "acknowledge",      "start-work",       "resolve", "reopen", "close",   "assign",  "set-priority",
      "request-transfer", "confirm-transfer", "comment", "link",   "unknown", "resolve", "assign"};
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): reproducible property test seed.
  std::mt19937 random(73);
  for (int run = 0; run < 200; ++run) {
    Value value;
    for (int step = 0; step < 200; ++step) {
      const Command command{.action = kActions[random() % kActions.size()],
                            .actor = random() % 2 == 0 ? "alice" : "bob",
                            .supervisor = random() % 2 == 0,
                            .assignee = random() % 3 == 0 ? "" : "alice",
                            .recipient_valid = random() % 2 == 0,
                            .resolution = static_cast<Resolution>(random() % 5),
                            .priority = static_cast<int>(random() % 6)};
      auto changed = apply(value, command);
      if (!changed) {
        continue;
      }
      EXPECT_NE(value.state, State::kClosed);
      if (changed->state == State::kAcknowledged || changed->state == State::kInProgress) {
        EXPECT_FALSE(changed->assignee.empty());
      }
      if (changed->state == State::kResolved || changed->state == State::kClosed) {
        EXPECT_NE(changed->resolution, Resolution::kNone);
      }
      EXPECT_GE(changed->priority, 1);
      EXPECT_LE(changed->priority, 4);
      value = *changed;
    }
  }
}
}  // namespace psim::domain::incident
