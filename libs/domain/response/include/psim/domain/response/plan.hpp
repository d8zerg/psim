#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <vector>

namespace psim::domain::response {
enum class Kind : std::uint8_t { kInstruction, kChecklist, kDecision, kCommand, kNotify, kWait, kEscalate, kAutomated };
enum class StepState : std::uint8_t { kPending = 1, kActive, kCompleted, kSkipped, kFailed, kCancelled };
enum class RunState : std::uint8_t { kRunning = 1, kCompleted, kCancelled };
enum class Operation : std::uint8_t { kComplete, kSkip, kRetry, kOverride, kSuccess, kFailure, kCancel };
enum class PlanFailure : std::uint8_t { kGraph, kFields, kState, kMandatory, kResult, kPermission };

struct Node {
  std::string id, next;
  Kind kind = Kind::kInstruction;
  bool mandatory = false;
  std::map<std::string, std::string> choices{};
  std::map<std::string, bool> items{};
  std::int64_t wait_ms = 0;
};

struct Plan {
  std::string start;
  std::vector<Node> nodes;
};

struct Result {
  std::vector<std::string> checked{};
  std::string choice{}, note{};
  bool overridden = false;
};

struct Execution {
  std::string id;
  StepState state = StepState::kPending;
  std::uint32_t attempt = 0;
  std::int64_t activated = 0, finished = 0;
  Result result{};
};

struct Run {
  RunState state = RunState::kRunning;
  std::uint64_t version = 0;
  std::vector<Execution> steps{};
  std::string active{};
  std::vector<std::string> blocking{};
  std::int64_t started = 0, finished = 0;
};

// Change values match the stable response event contract.
struct Change {
  int kind;
  std::string step;
  Run run;
};

[[nodiscard]] std::expected<void, PlanFailure> validate(const Plan& plan);
[[nodiscard]] std::vector<Change> start(const Plan& plan, std::int64_t now);
// The caller applies the returned snapshots atomically; failures leave the input unchanged.
[[nodiscard]] std::expected<std::vector<Change>, PlanFailure> advance(const Plan& plan, Run run,
                                                                      const std::string& step, Operation operation,
                                                                      Result result, bool supervisor, std::int64_t now);
}  // namespace psim::domain::response
