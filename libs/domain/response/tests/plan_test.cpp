#include "psim/domain/response/plan.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace psim::domain::response {
namespace {
Plan plan() {
  return {"decide",
          {{"decide", {}, Kind::kDecision, true, {{"yes", "check"}, {"no", "end"}}},
           {"check", "command", Kind::kChecklist, true, {}, {{"required", true}, {"optional", false}}},
           {"command", "end", Kind::kCommand, true},
           {"end", {}, Kind::kInstruction, false}}};
}

TEST(ResponsePlan, RejectsCyclesUnreachableAndInvalidReferences) {
  auto value = plan();
  ASSERT_TRUE(validate(value));
  value.nodes.back().next = "decide";
  EXPECT_FALSE(validate(value));
  value = plan();
  value.nodes.back().next = "missing";
  EXPECT_FALSE(validate(value));
  value = plan();
  value.nodes.push_back({"orphan", {}});
  EXPECT_FALSE(validate(value));
  value = plan();
  value.nodes.push_back(value.nodes.back());
  EXPECT_FALSE(validate(value));
  value = plan();
  value.nodes.front().choices.clear();
  EXPECT_FALSE(validate(value));
  value = plan();
  value.start.clear();
  EXPECT_FALSE(validate(value));
  value = plan();
  value.nodes.front().choices["yes"].clear();
  EXPECT_FALSE(validate(value));
  value = plan();
  value.nodes.front().next = "check";
  EXPECT_FALSE(validate(value));
  value = plan();
  value.nodes[1].items.clear();
  EXPECT_FALSE(validate(value));
  value = plan();
  value.nodes.back().kind = Kind::kWait;
  for (const auto duration : {0LL, -1LL, 2592000001LL}) {
    value.nodes.back().wait_ms = duration;
    EXPECT_FALSE(validate(value));
  }
  value.nodes.back().wait_ms = 1000;
  EXPECT_TRUE(validate(value));
  value = plan();
  value.nodes[1].choices["bad"] = "end";
  EXPECT_FALSE(validate(value));
}

TEST(ResponsePlan, ChoiceRemovesUnselectedMandatoryBranch) {
  const auto value = plan();
  auto run = start(value, 100).back().run;
  EXPECT_EQ(run.blocking.size(), 3);
  EXPECT_FALSE(advance(value, run, "check", Operation::kComplete, {}, false, 200));
  EXPECT_FALSE(advance(value, run, "decide", Operation::kSkip, {.note = "skip"}, true, 200));
  EXPECT_FALSE(advance(value, run, "decide", Operation::kComplete, {.choice = "unknown"}, false, 200));
  auto next = advance(value, run, "decide", Operation::kComplete, {.choice = "no"}, false, 200);
  ASSERT_TRUE(next);
  run = next->back().run;
  EXPECT_TRUE(run.blocking.empty());
  EXPECT_EQ(run.active, "end");
  next = advance(value, run, "end", Operation::kComplete, {}, false, 300);
  ASSERT_TRUE(next);
  EXPECT_EQ(next->back().run.state, RunState::kCompleted);
  EXPECT_FALSE(advance(value, next->back().run, "end", Operation::kRetry, {}, true, 400));
}

TEST(ResponsePlan, ChecklistAndCommandRequireTheirOwnResults) {
  const auto value = plan();
  auto run = start(value, 100).back().run;
  auto next = advance(value, run, "decide", Operation::kComplete, {.choice = "yes"}, false, 200);
  ASSERT_TRUE(next);
  run = next->back().run;
  for (const auto& checked : {std::vector<std::string>{}, std::vector<std::string>{"optional"},
                              std::vector<std::string>{"required", "required"}, std::vector<std::string>{"unknown"}}) {
    EXPECT_FALSE(advance(value, run, "check", Operation::kComplete, {.checked = checked}, false, 300));
  }
  next = advance(value, run, "check", Operation::kComplete, {.checked = {"required"}}, false, 300);
  ASSERT_TRUE(next);
  run = next->back().run;
  EXPECT_FALSE(advance(value, run, "command", Operation::kComplete, {}, false, 400));
  EXPECT_FALSE(advance(value, run, "command", Operation::kOverride, {.note = "reason"}, true, 400));
  next = advance(value, run, "command", Operation::kFailure, {.note = "device failed"}, false, 400);
  ASSERT_TRUE(next);
  run = next->back().run;
  EXPECT_FALSE(advance(value, run, "command", Operation::kOverride, {.note = "reason"}, false, 500));
  EXPECT_FALSE(advance(value, run, "command", Operation::kOverride, {}, true, 500));
  EXPECT_FALSE(advance(value, run, "command", Operation::kComplete, {}, false, 500));
  next = advance(value, run, "command", Operation::kRetry, {}, false, 500);
  ASSERT_TRUE(next);
  run = next->back().run;
  EXPECT_EQ(run.steps[2].attempt, 2);
  next = advance(value, run, "command", Operation::kFailure, {}, false, 600);
  ASSERT_TRUE(next);
  run = next->back().run;
  next = advance(value, run, "command", Operation::kOverride, {.note = "manual recovery"}, true, 700);
  ASSERT_TRUE(next);
  run = next->back().run;
  EXPECT_TRUE(run.steps[2].result.overridden);
  EXPECT_EQ(run.active, "end");
}

TEST(ResponsePlan, OptionalSkipAndCancellationPreserveCompletedWork) {
  auto value = plan();
  value.nodes[2].mandatory = false;
  auto run = start(value, 100).back().run;
  auto next = advance(value, run, "decide", Operation::kComplete, {.choice = "yes"}, false, 200);
  ASSERT_TRUE(next);
  run = next->back().run;
  next = advance(value, run, "check", Operation::kComplete, {.checked = {"required"}}, false, 300);
  ASSERT_TRUE(next);
  run = next->back().run;
  EXPECT_FALSE(advance(value, run, "command", Operation::kSkip, {}, false, 400));
  next = advance(value, run, "command", Operation::kSkip, {.note = "not needed"}, false, 400);
  ASSERT_TRUE(next);
  run = next->back().run;
  EXPECT_EQ(run.steps[2].state, StepState::kSkipped);
  next = advance(value, run, "end", Operation::kCancel, {}, true, 500);
  ASSERT_TRUE(next);
  run = next->back().run;
  EXPECT_EQ(run.state, RunState::kCancelled);
  EXPECT_TRUE(run.blocking.empty());
  EXPECT_EQ(run.steps.front().state, StepState::kCompleted);
  EXPECT_EQ(run.steps.back().state, StepState::kCancelled);
}

TEST(ResponsePlan, SystemCompletionAndRetryUseNewAttempt) {
  const Plan value{"wait", {{"wait", {}, Kind::kWait, true, {}, {}, 500}}};
  auto run = start(value, 100).back().run;
  EXPECT_FALSE(advance(value, run, "wait", Operation::kComplete, {}, false, 200));
  auto next = advance(value, run, "wait", Operation::kSuccess, {}, false, 600);
  ASSERT_TRUE(next);
  EXPECT_EQ(next->back().run.finished, 600);
  auto decision = plan();
  decision.nodes.front().mandatory = false;
  EXPECT_FALSE(
      advance(decision, start(decision, 100).back().run, "decide", Operation::kSkip, {.note = "skip"}, true, 200));
  next = advance(value, run, "wait", Operation::kRetry, {}, false, 200);
  EXPECT_FALSE(next);
}
}  // namespace
}  // namespace psim::domain::response
