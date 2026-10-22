#include "model.hpp"

#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/common/v1/envelope.pb.h"
#include "psim/domain/response/plan.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/response/v1/plan.pb.h"
#include "psim/response/v1/run.pb.h"

namespace psim::response_engine {
using Time = google::protobuf::util::TimeUtil;

namespace {
template <typename T>
bool intersection(const T& left, const T& right) {
  return left.empty() || right.empty()
         || std::ranges::any_of(left, [&](const auto& item) { return std::ranges::find(right, item) != right.end(); });
}

template <typename T, typename V>
bool contains(const T& list, const V& value) {
  return list.empty() || std::ranges::find(list, value) != list.end();
}
}  // namespace

p::Result<d::Plan> model(const response::v1::ResponsePlan& plan) {
  if (!p::stream::uuid(plan.plan_id()) || plan.name().empty() || plan.name().size() > 200 || plan.bindings_size() > 200
      || plan.ByteSizeLong() > 524288) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid plan identity, name or size");
  }
  d::Plan output{plan.start_step_id(), {}};
  for (const auto& binding : plan.bindings()) {
    if (!p::stream::uuid(binding.incident_type_id()) || binding.site_ids_size() > 1000 || binding.zone_ids_size() > 1000
        || std::ranges::any_of(binding.site_ids(), [](const auto& id) { return !p::stream::uuid(id); })
        || std::ranges::any_of(binding.zone_ids(), [](const auto& id) { return !p::stream::uuid(id); })
        || std::ranges::any_of(binding.priorities(), [](int value) { return value < 1 || value > 4; })) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid binding scope");
    }
  }
  for (const auto& step : plan.steps()) {
    d::Node current{step.step_id(), step.next_step_id()};
    current.mandatory = step.mandatory();
    if (step.title().empty() || step.title().size() > 500) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid step title");
    }
    switch (step.params_case()) {
      case response::v1::Step::kInstruction:
        if (step.instruction().text().size() > 8000) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "instruction exceeds limit");
        }
        break;
      case response::v1::Step::kChecklist:
        current.kind = d::Kind::kChecklist;
        if (step.checklist().items_size() > 200) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "checklist exceeds limit");
        }
        for (const auto& item : step.checklist().items()) {
          if (item.item_id().empty() || item.item_id().size() > 128 || item.text().empty() || item.text().size() > 8000
              || !current.items.emplace(item.item_id(), item.required()).second) {
            return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid checklist item");
          }
        }
        break;
      case response::v1::Step::kDecision:
        current.kind = d::Kind::kDecision;
        for (const auto& option : step.decision().options()) {
          if (option.label().empty() || option.option_id().size() > 128
              || !current.choices.emplace(option.option_id(), option.next_step_id()).second) {
            return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid decision option");
          }
        }
        break;
      case response::v1::Step::kCommand:
        current.kind = d::Kind::kCommand;
        if (step.command().capability().empty() || step.command().capability().size() > 128
            || (!p::stream::uuid(step.command().device_id()) && step.command().target_expression() != "zone.devices")
            || step.command().parameters_size() > 32 || step.command().ByteSizeLong() > 16384) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid command target or parameters");
        }
        break;
      case response::v1::Step::kNotify:
        current.kind = d::Kind::kNotify;
        if (step.notify().recipients().empty() || step.notify().recipients_size() > 100
            || step.notify().channels().empty() || step.notify().template_().size() > 8000) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid notification");
        }
        for (const auto& recipient : step.notify().recipients()) {
          if ((recipient.user_id().empty() && recipient.role().empty()) || recipient.ByteSizeLong() > 256) {
            return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid recipient");
          }
        }
        for (const int channel : step.notify().channels()) {
          if (channel < 1 || channel > 3) {
            return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid notification channel");
          }
        }
        break;
      case response::v1::Step::kWait:
        current.kind = d::Kind::kWait;
        if (!step.wait().has_duration() || step.wait().duration().seconds() < 0
            || step.wait().duration().seconds() > 2592000 || step.wait().duration().nanos() < 0
            || step.wait().duration().nanos() >= 1000000000) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid wait duration");
        }
        current.wait_ms = Time::DurationToMilliseconds(step.wait().duration());
        break;
      case response::v1::Step::kEscalate:
        current.kind = d::Kind::kEscalate;
        if (step.escalate().level() == 0 || step.escalate().level() > 100) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid escalation level");
        }
        break;
      case response::v1::Step::kAutomated:
        current.kind = d::Kind::kAutomated;
        if (!step.automated().webhook_url().starts_with("https://") || !step.automated().has_timeout()
            || step.automated().timeout().seconds() < 1 || step.automated().timeout().seconds() > 30
            || step.automated().timeout().nanos() != 0) {
          return p::fail(p::ErrorCode::kCommonInvalidArgument, "HTTPS handler with bounded timeout required");
        }
        break;
      default:
        return p::fail(p::ErrorCode::kCommonInvalidArgument, "step parameters required");
    }
    output.nodes.push_back(std::move(current));
  }
  if (!d::validate(output)) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid plan graph or step fields");
  }
  return output;
}

d::Run model(const response::v1::ResponseRun& run) {
  d::Run output;
  output.state = static_cast<d::RunState>(run.state());
  output.version = run.version();
  output.started = Time::TimestampToMilliseconds(run.started_at());
  output.finished = run.has_finished_at() ? Time::TimestampToMilliseconds(run.finished_at()) : 0;
  if (!run.active_step_ids().empty()) {
    output.active = run.active_step_ids(0);
  }
  for (const auto& step : run.steps()) {
    d::Execution execution{step.step_id()};
    execution.state = static_cast<d::StepState>(step.state());
    execution.attempt = step.attempt();
    execution.activated = step.has_activated_at() ? Time::TimestampToMilliseconds(step.activated_at()) : 0;
    execution.finished = step.has_finished_at() ? Time::TimestampToMilliseconds(step.finished_at()) : 0;
    execution.result.checked.assign(step.result().checked_item_ids().begin(), step.result().checked_item_ids().end());
    execution.result.choice = step.result().decision_option_id();
    execution.result.note = step.result().note();
    execution.result.overridden = step.result().overridden();
    output.steps.push_back(std::move(execution));
  }
  output.blocking.assign(run.blocking_step_ids().begin(), run.blocking_step_ids().end());
  return output;
}

response::v1::ResponseRun snapshot(const d::Run& run, const response::v1::ResponseRun& identity,
                                   const response::v1::ResponsePlan& plan, std::string actor) {
  auto output = identity;
  output.set_state(static_cast<response::v1::RunState>(run.state));
  output.set_version(run.version);
  *output.mutable_started_at() = Time::MillisecondsToTimestamp(run.started);
  if (run.finished != 0) {
    *output.mutable_finished_at() = Time::MillisecondsToTimestamp(run.finished);
  }
  output.clear_active_step_ids();
  if (!run.active.empty()) {
    output.add_active_step_ids(run.active);
  }
  output.clear_blocking_step_ids();
  for (const auto& id : run.blocking) {
    output.add_blocking_step_ids(id);
  }
  output.clear_steps();
  for (const auto& current : run.steps) {
    auto* step = output.add_steps();
    step->set_step_id(current.id);
    step->set_state(static_cast<response::v1::StepState>(current.state));
    step->set_attempt(current.attempt);
    if (current.activated != 0) {
      *step->mutable_activated_at() = Time::MillisecondsToTimestamp(current.activated);
    }
    if (current.finished != 0) {
      *step->mutable_finished_at() = Time::MillisecondsToTimestamp(current.finished);
    }
    const auto previous = std::ranges::find(identity.steps(), current.id, &response::v1::StepExecution::step_id);
    if (previous != identity.steps().end()) {
      *step->mutable_actor() = previous->actor();
      *step->mutable_result()->mutable_command_ids() = previous->result().command_ids();
    }
    if (previous == identity.steps().end() || previous->state() != step->state()
        || previous->attempt() != step->attempt()) {
      step->mutable_actor()->set_id(actor);
      step->mutable_actor()->set_kind(actor == "response-engine" ? common::v1::ACTOR_KIND_SERVICE
                                                                 : common::v1::ACTOR_KIND_USER);
    }
    auto* result = step->mutable_result();
    for (const auto& id : current.result.checked) {
      result->add_checked_item_ids(id);
    }
    result->set_decision_option_id(current.result.choice);
    result->set_note(current.result.note);
    result->set_overridden(current.result.overridden);
    const auto node = std::ranges::find(plan.steps(), current.id, &response::v1::Step::step_id);
    if (node != plan.steps().end() && node->has_command() && current.attempt > 0 && previous != identity.steps().end()
        && previous->attempt() != current.attempt) {
      result->clear_command_ids();
    }
  }
  return output;
}

int specificity(const response::v1::PlanBinding& binding) {
  return (binding.zone_ids().empty() ? 0 : 4) + (binding.site_ids().empty() ? 0 : 2)
         + (binding.priorities().empty() ? 0 : 1);
}

bool overlaps(const response::v1::PlanBinding& left, const response::v1::PlanBinding& right) {
  return left.incident_type_id() == right.incident_type_id() && specificity(left) == specificity(right)
         && left.binding_priority() == right.binding_priority() && intersection(left.site_ids(), right.site_ids())
         && intersection(left.zone_ids(), right.zone_ids()) && intersection(left.priorities(), right.priorities());
}

bool matches(const response::v1::PlanBinding& binding, const std::string& type, const std::string& site,
             const std::string& zone, int priority) {
  return binding.incident_type_id() == type && contains(binding.site_ids(), site) && contains(binding.zone_ids(), zone)
         && contains(binding.priorities(), priority);
}

std::string effect_id(const response::v1::ResponseRun& run, const std::string& step, std::uint32_t attempt,
                      const std::string& target) {
  return p::stream::uuid5(run.run_id(), {step, std::to_string(attempt), target});
}

Json replay(const response::v1::ResponsePlan& plan, const Json& history) {
  const auto parsed = model(plan);
  if (!parsed) {
    throw std::invalid_argument("invalid replay plan");
  }
  auto changes = d::start(*parsed, 1);
  auto run = changes.back().run;
  Json effects = Json::array();
  std::size_t action = 0;
  for (int limit = 0; limit < 1000 && run.state == d::RunState::kRunning; ++limit) {
    const auto& current = *std::ranges::find(plan.steps(), run.active, &response::v1::Step::step_id);
    auto operation = d::Operation::kSuccess;
    d::Result result;
    if (current.has_instruction() || current.has_checklist() || current.has_decision()) {
      if (action >= history.size()) {
        break;
      }
      const auto& input = history.at(action++);
      if (input.at("stepId") != run.active) {
        throw std::invalid_argument("replay action order differs from active step");
      }
      operation = d::Operation::kComplete;
      result.checked = input.value("checkedItemIds", std::vector<std::string>{});
      result.choice = input.value("decisionOptionId", std::string{});
    } else {
      effects.push_back({{"stepId", run.active}, {"kind", current.params_case()}, {"simulated", true}});
    }
    auto next = d::advance(*parsed, run, run.active, operation, result, false, run.started + limit + 1);
    if (!next) {
      throw std::invalid_argument("invalid replay action");
    }
    run = next->back().run;
  }
  if (action != history.size()) {
    throw std::invalid_argument("unconsumed replay actions");
  }
  return {{"state", static_cast<int>(run.state)},
          {"activeStepId", run.active},
          {"effects", effects},
          {"externalCalls", 0},
          {"version", run.version}};
}
}  // namespace psim::response_engine
