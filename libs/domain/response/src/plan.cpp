#include "psim/domain/response/plan.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace psim::domain::response {
namespace {
const Node& node(const Plan& plan, const std::string& id) {
  return *std::ranges::find(plan.nodes, id, &Node::id);
}

void reachable(const Plan& plan, const std::string& id, std::set<std::string>& found) {
  if (id.empty() || !found.insert(id).second) {
    return;
  }
  const auto& current = node(plan, id);
  reachable(plan, current.next, found);
  for (const auto& [choice, target] : current.choices) {
    (void)choice;
    reachable(plan, target, found);
  }
}

void emit(const Plan& plan, Run& run, int kind, const std::string& step, std::vector<Change>& changes) {
  std::set<std::string> remaining;
  reachable(plan, run.active, remaining);
  const auto previous = run.blocking;
  run.blocking.clear();
  if (run.state == RunState::kRunning) {
    for (const auto& id : remaining) {
      if (node(plan, id).mandatory) {
        run.blocking.push_back(id);
      }
    }
  }
  ++run.version;
  changes.push_back({kind, step, run});
  if (kind != 6 && previous != run.blocking) {
    ++run.version;
    changes.push_back({6, step, run});
  }
}

void activate(const Plan& plan, Run& run, const std::string& id, std::int64_t now, std::vector<Change>& changes) {
  run.active = id;
  if (id.empty()) {
    run.state = RunState::kCompleted;
    run.finished = now;
    emit(plan, run, 7, {}, changes);
    return;
  }
  auto& execution = *std::ranges::find(run.steps, id, &Execution::id);
  execution.state = StepState::kActive;
  ++execution.attempt;
  execution.activated = now;
  execution.finished = 0;
  execution.result = {};
  emit(plan, run, 2, id, changes);
}
}  // namespace

std::expected<void, PlanFailure> validate(const Plan& plan) {
  if (plan.nodes.empty() || plan.nodes.size() > 200 || plan.start.empty()) {
    return std::unexpected(PlanFailure::kGraph);
  }
  std::map<std::string, int> colors;
  for (const auto& current : plan.nodes) {
    if (current.id.empty() || current.id.size() > 128 || !colors.emplace(current.id, 0).second
        || (current.kind == Kind::kWait && (current.wait_ms <= 0 || current.wait_ms > 2592000000LL))
        || (current.kind == Kind::kChecklist && current.items.empty())
        || (current.kind == Kind::kDecision && (current.choices.size() < 2 || !current.next.empty()))
        || (current.kind != Kind::kDecision && !current.choices.empty())) {
      return std::unexpected(PlanFailure::kFields);
    }
    for (const auto& [id, target] : current.choices) {
      if (id.empty() || target.empty()) {
        return std::unexpected(PlanFailure::kFields);
      }
    }
  }
  const std::function<bool(const std::string&)> visit = [&](const std::string& id) {
    if (id.empty()) {
      return true;
    }
    auto found = colors.find(id);
    if (found == colors.end() || found->second == 1) {
      return false;
    }
    if (found->second == 2) {
      return true;
    }
    found->second = 1;
    const auto& current = node(plan, id);
    if (!visit(current.next)) {
      return false;
    }
    for (const auto& [choice, target] : current.choices) {
      (void)choice;
      if (!visit(target)) {
        return false;
      }
    }
    found->second = 2;
    return true;
  };
  if (!visit(plan.start) || std::ranges::any_of(colors, [](const auto& entry) { return entry.second != 2; })) {
    return std::unexpected(PlanFailure::kGraph);
  }
  return {};
}

std::vector<Change> start(const Plan& plan, std::int64_t now) {
  Run run;
  run.started = now;
  for (const auto& current : plan.nodes) {
    run.steps.push_back({current.id});
  }
  std::vector<Change> changes;
  run.active = plan.start;
  emit(plan, run, 1, {}, changes);
  activate(plan, run, plan.start, now, changes);
  return changes;
}

std::expected<std::vector<Change>, PlanFailure> advance(const Plan& plan, Run run, const std::string& step,
                                                        Operation operation, Result result, bool supervisor,
                                                        std::int64_t now) {
  if (run.state != RunState::kRunning || run.active.empty()) {
    return std::unexpected(PlanFailure::kState);
  }
  std::vector<Change> changes;
  if (operation == Operation::kCancel) {
    run.state = RunState::kCancelled;
    run.active.clear();
    run.finished = now;
    for (auto& execution : run.steps) {
      if (execution.state == StepState::kActive || execution.state == StepState::kPending
          || execution.state == StepState::kFailed) {
        execution.state = StepState::kCancelled;
        execution.finished = now;
      }
    }
    emit(plan, run, 8, {}, changes);
    return changes;
  }
  if (step != run.active) {
    return std::unexpected(PlanFailure::kState);
  }
  const auto& current = node(plan, step);
  auto& execution = *std::ranges::find(run.steps, step, &Execution::id);
  if (operation == Operation::kRetry || operation == Operation::kOverride) {
    if (execution.state != StepState::kFailed) {
      return std::unexpected(PlanFailure::kState);
    }
    if (operation == Operation::kRetry) {
      activate(plan, run, step, now, changes);
      return changes;
    }
    if (!supervisor || result.note.empty()) {
      return std::unexpected(PlanFailure::kPermission);
    }
    result.overridden = true;
  } else if (execution.state != StepState::kActive
             && (operation != Operation::kSkip || execution.state != StepState::kFailed)) {
    return std::unexpected(PlanFailure::kState);
  }
  auto next = current.next;
  StepState state = StepState::kCompleted;
  int kind = 3;
  if (operation == Operation::kSkip) {
    if (current.mandatory) {
      return std::unexpected(PlanFailure::kMandatory);
    }
    if (result.note.empty() || current.kind == Kind::kDecision) {
      return std::unexpected(PlanFailure::kResult);
    }
    state = StepState::kSkipped;
    kind = 4;
  } else if (operation == Operation::kFailure) {
    state = StepState::kFailed;
    kind = 5;
  } else if (operation == Operation::kComplete) {
    if (current.kind == Kind::kChecklist) {
      const std::set<std::string> checked(result.checked.begin(), result.checked.end());
      if (checked.size() != result.checked.size()
          || std::ranges::any_of(checked, [&](const auto& id) { return !current.items.contains(id); })
          || std::ranges::any_of(current.items,
                                 [&](const auto& item) { return item.second && !checked.contains(item.first); })) {
        return std::unexpected(PlanFailure::kResult);
      }
    } else if (current.kind == Kind::kDecision) {
      const auto found = current.choices.find(result.choice);
      if (found == current.choices.end()) {
        return std::unexpected(PlanFailure::kResult);
      }
      next = found->second;
    } else if (current.kind != Kind::kInstruction) {
      return std::unexpected(PlanFailure::kState);
    }
  } else if (operation != Operation::kSuccess && operation != Operation::kOverride) {
    return std::unexpected(PlanFailure::kState);
  }
  execution.state = state;
  execution.result = std::move(result);
  execution.finished = now;
  if (state != StepState::kFailed) {
    run.active = next;
  }
  emit(plan, run, kind, step, changes);
  if (state != StepState::kFailed) {
    activate(plan, run, next, now, changes);
  }
  return changes;
}
}  // namespace psim::domain::response
