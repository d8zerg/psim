#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "psim/catalog/v1/catalog.pb.h"
#include "psim/catalog/v1/maintenance.pb.h"
#include "psim/common/v1/types.pb.h"
#include "psim/domain/processing/expression.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/event.pb.h"
#include "psim/processing/v1/rules.pb.h"
#include "psim/processing/v1/signal.pb.h"
#include "rules.hpp"

namespace psim::correlation::rules {
namespace {
using Time = google::protobuf::util::TimeUtil;
using Scope = processing::v1::CorrelationScope;
constexpr auto kRuleInvalid = p::ErrorCode::kProcessingRuleInvalid;

std::int64_t occurred(const Event& event) {
  return Time::TimestampToMilliseconds(event.envelope().occurred_at());
}

std::int64_t received(const Event& event, std::int64_t fallback) {
  return event.envelope().has_received_at() ? Time::TimestampToMilliseconds(event.envelope().received_at()) : fallback;
}

std::string location(const Event& event, const Context& context, catalog::v1::LocationKind kind) {
  auto id = event.event().zone_id();
  for (unsigned int depth = 0; depth < 8 && !id.empty(); ++depth) {
    const auto found = context.locations.find(id);
    if (found == context.locations.end() || found->second.site_id() != event.event().site_id()) {
      return {};
    }
    if (found->second.kind() == kind) {
      return id;
    }
    id = found->second.parent_id();
  }
  return {};
}

std::string scope_id(const Event& event, const Context& context, Scope scope) {
  switch (scope) {
    case processing::v1::CORRELATION_SCOPE_SOURCE:
      return event.event().source_id();
    case processing::v1::CORRELATION_SCOPE_DEVICE:
      return event.event().device_id();
    case processing::v1::CORRELATION_SCOPE_ZONE:
      return event.event().zone_id();
    case processing::v1::CORRELATION_SCOPE_FLOOR:
      return location(event, context, catalog::v1::LOCATION_KIND_FLOOR);
    case processing::v1::CORRELATION_SCOPE_BUILDING:
      return location(event, context, catalog::v1::LOCATION_KIND_BUILDING);
    case processing::v1::CORRELATION_SCOPE_SITE:
      return event.event().site_id();
    default:
      return {};
  }
}

expr::Resolve resolver(const Event& event, const CompiledRule& rule, const Context& context, std::size_t count) {
  return [&event, &rule, &context, count](std::string_view field) -> std::optional<expr::Value> {
    const auto& value = event.event();
    if (field == "event.type") {
      return value.type();
    }
    if (field == "event.raw_code") {
      return value.raw_code();
    }
    if (field == "event.site_id") {
      return value.site_id();
    }
    if (field == "event.zone_id") {
      return value.zone_id();
    }
    if (field == "event.device_id") {
      return value.device_id();
    }
    if (field == "event.source_id") {
      return value.source_id();
    }
    if (field == "event.connector_id") {
      return value.connector_id();
    }
    if (field == "event.floor_id") {
      return location(event, context, catalog::v1::LOCATION_KIND_FLOOR);
    }
    if (field == "event.building_id") {
      return location(event, context, catalog::v1::LOCATION_KIND_BUILDING);
    }
    if (field == "event.severity") {
      return static_cast<std::int64_t>(value.severity());
    }
    if (field == "event.event_class") {
      return static_cast<std::int64_t>(value.event_class());
    }
    if (field == "event.source_epoch") {
      return static_cast<std::int64_t>(value.source_epoch());
    }
    if (field == "event.live_observation") {
      return value.live_observation();
    }
    if (field == "rule.name") {
      return rule.rule.name();
    }
    if (field == "rule.rule_id") {
      return rule.rule.rule_id();
    }
    if (field == "rule.version") {
      return static_cast<std::int64_t>(rule.rule.version());
    }
    if (field == "matches.count") {
      return static_cast<std::int64_t>(count);
    }
    constexpr std::string_view kAttributes = "event.attributes.";
    if (field.starts_with(kAttributes)) {
      auto found = value.attributes().find(std::string(field.substr(kAttributes.size())));
      if (found != value.attributes().end()) {
        return found->second;
      }
    }
    return {};
  };
}

bool selector(std::string_view pattern, std::string_view type) {
  return pattern.ends_with(".*") ? type.starts_with(pattern.substr(0, pattern.size() - 1)) : pattern == type;
}

p::Result<bool> matches(const CompiledFilter& filter, const Event& event, const CompiledRule& rule,
                        const Context& context, bool inventory = false) {
  const auto& value = event.event();
  const auto& spec = filter.filter;
  if ((!spec.site_ids().empty() && !std::ranges::contains(spec.site_ids(), value.site_id()))
      || (!spec.zone_ids().empty() && !std::ranges::contains(spec.zone_ids(), value.zone_id()))) {
    return false;
  }
  if (!inventory
      && (value.severity() < spec.min_severity()
          || !std::ranges::any_of(spec.types(), [&](const auto& type) { return selector(type, value.type()); }))) {
    return false;
  }
  auto result = filter.condition.evaluate(resolver(event, rule, context, 1));
  if (!result) {
    return p::fail(kRuleInvalid, "rule condition evaluation failed: " + result.error());
  }
  return std::get<bool>(*result);
}

struct Candidate {
  const Event* event;
  std::uint32_t mask;
};

struct Pending {
  const Event* trigger;
  std::int64_t deadline;
};

struct State {
  const Event* subject = nullptr;
  std::deque<Candidate> events{};
  std::vector<Pending> pending{};
  std::int64_t next = 0;
  std::int64_t last_signal = std::numeric_limits<std::int64_t>::min();
  bool seen = false;
};

bool maintenance_matches(const catalog::v1::MaintenanceWindow& maintenance, const CompiledRule& rule,
                         const Event& event, std::int64_t receipt) {
  if (maintenance.site_id() != event.event().site_id() || !maintenance.has_activated_at()) {
    return false;
  }
  const auto begin = std::max(Time::TimestampToMilliseconds(maintenance.starts_at()),
                              Time::TimestampToMilliseconds(maintenance.activated_at()));
  auto end = Time::TimestampToMilliseconds(maintenance.ends_at());
  if (maintenance.has_completed_at()) {
    end = std::min(end, Time::TimestampToMilliseconds(maintenance.completed_at()));
  }
  if (receipt < begin || receipt >= end) {
    return false;
  }
  if (maintenance.has_device_id() && maintenance.device_id() != event.event().device_id()) {
    return false;
  }
  if (maintenance.has_zone_id() && maintenance.zone_id() != event.event().zone_id()) {
    return false;
  }
  return std::ranges::contains(maintenance.suppression().rule_ids(), rule.rule.rule_id())
         || std::ranges::any_of(maintenance.suppression().event_types(),
                                [&](const auto& type) { return selector(type, event.event().type()); });
}

class Replay {
 public:
  Replay(const CompiledRule& rule, const Context& context, std::string tenant, Limits limits)
      : rule_(&rule),
        context_(&context),
        tenant_(std::move(tenant)),
        limits_(limits),
        window_(Time::DurationToMilliseconds(rule.rule.window())),
        suppression_(Time::DurationToMilliseconds(rule.rule.suppression())) {}

  p::Result<ReplayResult> run(const std::vector<Event>& events, std::int64_t from, std::int64_t to) {
    const bool periodic = rule_->rule.has_absence() && !rule_->trigger;
    if (periodic) {
      if (context_->subjects.empty()) {
        return p::fail(kRuleInvalid, "absence replay requires a catalog inventory");
      }
      for (const auto& subject : context_->subjects) {
        if (subject.envelope().tenant_id() != tenant_) {
          continue;
        }
        auto selected = matches(rule_->filter, subject, *rule_, *context_, true);
        if (!selected) {
          return std::unexpected(selected.error());
        }
        if (!*selected) {
          continue;
        }
        auto id = key(subject, rule_->rule.correlation_scope());
        if (id.empty()) {
          return p::fail(kRuleInvalid, "inventory correlation scope is unavailable");
        }
        auto& state = states_[id];
        state.subject = &subject;
        state.next = from + window_;
        if (states_.size() > limits_.max_keys) {
          return p::fail(kRuleInvalid, "replay key limit exceeded");
        }
      }
    }
    for (const auto& event : events) {
      if (event.envelope().tenant_id() != tenant_ || occurred(event) < from || occurred(event) >= to) {
        continue;
      }
      ++result_.processed;
      auto base = matches(rule_->filter, event, *rule_, *context_);
      if (!base) {
        return std::unexpected(base.error());
      }
      if (!*base) {
        continue;
      }
      const auto id = key(event, rule_->rule.correlation_scope());
      if (id.empty()) {
        return p::fail(kRuleInvalid, "event correlation scope is unavailable");
      }
      if (periodic && !states_.contains(id)) {
        return p::fail(kRuleInvalid, "event scope absent from inventory");
      }
      auto& state = states_[id];
      if (states_.size() > limits_.max_keys) {
        return p::fail(kRuleInvalid, "replay key limit exceeded");
      }
      auto timers = due(state, occurred(event), true);
      if (!timers) {
        return std::unexpected(timers.error());
      }
      auto step = consume(state, event);
      if (!step) {
        return std::unexpected(step.error());
      }
    }
    for (auto& [id, state] : states_) {
      (void)id;
      auto timers = due(state, to, false);
      if (!timers) {
        return std::unexpected(timers.error());
      }
    }
    std::ranges::sort(result_.decisions, [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    return std::move(result_);
  }

 private:
  std::string key(const Event& event, Scope scope) const {
    const auto id = scope_id(event, *context_, scope);
    if (id.empty() || event.event().site_id().empty()) {
      return {};
    }
    return tenant_ + ":" + event.event().site_id() + ":" + std::to_string(scope) + ":" + id;
  }

  p::Result<const catalog::v1::MaintenanceWindow*> policy_at(const std::string& id, std::int64_t receipt) {
    const catalog::v1::MaintenanceWindow* selected = nullptr;
    bool unknown = false;
    for (const auto& policy : context_->maintenance) {
      if (++work_ > 50000000) {
        return p::fail(kRuleInvalid, "replay operation limit exceeded");
      }
      if (policy.maintenance_id() != id) {
        continue;
      }
      auto updated = std::int64_t{0};
      if (policy.has_updated_at()) {
        updated = Time::TimestampToMilliseconds(policy.updated_at());
      } else if (policy.has_activated_at()) {
        updated = Time::TimestampToMilliseconds(policy.activated_at());
      }
      if (updated <= receipt && (selected == nullptr || policy.version() > selected->version())) {
        selected = &policy;
      }
      if (policy.has_activated_at() && Time::TimestampToMilliseconds(policy.activated_at()) <= receipt) {
        unknown = true;
      }
    }
    if (selected == nullptr && unknown) {
      return p::fail(kRuleInvalid, "maintenance policy history does not cover receipt time");
    }
    return selected;
  }

  p::Result<void> emit(State& state, const Event& latest, std::vector<const Event*> evidence, std::int64_t at,
                       bool absence = false) {
    if (state.last_signal != std::numeric_limits<std::int64_t>::min() && at - state.last_signal < suppression_) {
      return {};
    }
    if (result_.decisions.size() >= limits_.max_signals) {
      return p::fail(kRuleInvalid, "replay signal limit exceeded");
    }
    const auto& rule = rule_->rule;
    Decision decision;
    auto& signal = decision.signal;
    signal.mutable_origin()->set_kind(processing::v1::SIGNAL_ORIGIN_KIND_RULE);
    signal.mutable_origin()->set_rule_id(rule.rule_id());
    signal.mutable_origin()->set_rule_version(rule.version());
    signal.set_correlation_key(key(latest, rule.correlation_scope()));
    const auto grouping_scope =
        rule.output().grouping_scope() == 0 ? rule.correlation_scope() : rule.output().grouping_scope();
    signal.set_grouping_key(key(latest, grouping_scope));
    if (signal.grouping_key().empty()) {
      return p::fail(kRuleInvalid, "grouping scope is unavailable");
    }
    signal.set_grouping_key(signal.grouping_key() + ":" + rule.output().incident_type_id());
    signal.set_incident_type_id(rule.output().incident_type_id());
    signal.set_site_id(latest.event().site_id());
    if (grouping_scope <= processing::v1::CORRELATION_SCOPE_ZONE) {
      signal.set_zone_id(latest.event().zone_id());
    }
    auto priority = rule.output().priority();
    const auto resolve = resolver(latest, *rule_, *context_, evidence.size());
    if (rule_->priority) {
      auto value = rule_->priority->evaluate(resolve);
      if (!value) {
        return p::fail(kRuleInvalid, "priority evaluation failed");
      }
      const auto number = std::get<std::int64_t>(*value);
      if (number < 1 || number > 4) {
        return p::fail(kRuleInvalid, "priority outside 1..4");
      }
      priority = static_cast<common::v1::Priority>(number);
    }
    signal.set_priority(priority);
    std::string summary;
    for (const auto& part : rule_->summary) {
      summary += part.literal;
      if (part.expression) {
        auto text = part.expression->evaluate(resolve);
        if (!text) {
          return p::fail(kRuleInvalid, "summary evaluation failed");
        }
        summary += std::get<std::string>(*text);
      }
      if (summary.size() > 1024) {
        return p::fail(kRuleInvalid, "summary output limit exceeded");
      }
    }
    signal.set_summary(summary.empty() ? rule.name() : summary);
    std::ranges::sort(evidence, [](const auto* lhs, const auto* rhs) {
      return lhs->envelope().message_id() < rhs->envelope().message_id();
    });
    std::vector<std::string> identity{tenant_, rule.rule_id(), std::to_string(rule.version()),
                                      signal.correlation_key()};
    bool fire = std::ranges::any_of(rule.filter().types(), [](const auto& type) { return type.starts_with("fire."); });
    std::int64_t first = at;
    std::int64_t last = 0;
    for (const auto* event : evidence) {
      signal.add_event_ids(event->envelope().message_id());
      identity.push_back(event->envelope().message_id());
      first = std::min(first, occurred(*event));
      last = std::max(last, occurred(*event));
      fire = fire || event->event().type().starts_with("fire.");
    }
    if (!evidence.empty()) {
      *signal.mutable_first_event_at() = Time::MillisecondsToTimestamp(first);
      *signal.mutable_last_event_at() = Time::MillisecondsToTimestamp(last);
    }
    if (absence) {
      *signal.mutable_absence_deadline() = Time::MillisecondsToTimestamp(at);
      identity.push_back(std::to_string(at));
    }
    decision.id = p::stream::uuid5("c8b07c6e-a8aa-5eb0-9744-705b51f8c211", identity);
    if (!fire && priority != common::v1::PRIORITY_P1) {
      const auto receipt = absence ? at : received(latest, at);
      std::set<std::string> ids;
      for (const auto& policy : context_->maintenance) {
        if (policy.site_id() == latest.event().site_id()
            && ((policy.has_device_id() && policy.device_id() == latest.event().device_id())
                || (policy.has_zone_id() && policy.zone_id() == latest.event().zone_id()))) {
          ids.insert(policy.maintenance_id());
        }
      }
      for (const auto& id : ids) {
        auto selected = policy_at(id, receipt);
        if (!selected) {
          return std::unexpected(selected.error());
        }
        const auto* maintenance = *selected;
        if (maintenance == nullptr || !maintenance_matches(*maintenance, *rule_, latest, receipt)) {
          continue;
        }
        if (evidence.empty()
            && ((maintenance->has_device_id() && rule.correlation_scope() > processing::v1::CORRELATION_SCOPE_DEVICE)
                || (maintenance->has_zone_id() && rule.correlation_scope() > processing::v1::CORRELATION_SCOPE_ZONE))) {
          continue;
        }
        bool covered = true;
        std::vector<std::pair<std::string, std::uint64_t>> versions{{id, maintenance->version()}};
        for (const auto* event : evidence) {
          const auto event_receipt = absence ? at : received(*event, occurred(*event));
          auto version = policy_at(id, event_receipt);
          if (!version) {
            return std::unexpected(version.error());
          }
          if (*version == nullptr || !maintenance_matches(**version, *rule_, *event, event_receipt)) {
            covered = false;
            break;
          }
          versions.emplace_back(id, (*version)->version());
        }
        if (covered) {
          decision.maintenance_ids.push_back(id);
          decision.maintenance_versions.insert(decision.maintenance_versions.end(), versions.begin(), versions.end());
        }
      }
    }
    std::ranges::sort(decision.maintenance_versions);
    auto versions_end = std::ranges::unique(decision.maintenance_versions);
    decision.maintenance_versions.erase(versions_end.begin(), versions_end.end());
    std::ranges::sort(decision.maintenance_ids);
    auto ids_end = std::ranges::unique(decision.maintenance_ids);
    decision.maintenance_ids.erase(ids_end.begin(), ids_end.end());
    decision.suppressed = !decision.maintenance_ids.empty();
    if (!decision.suppressed) {
      state.last_signal = at;
    }
    result_.decisions.push_back(std::move(decision));
    return {};
  }

  p::Result<void> due(State& state, std::int64_t until, bool inclusive) {
    auto reached = [&](std::int64_t deadline) {
      return inclusive ? deadline <= until : deadline < until;
    };
    if (state.subject != nullptr) {
      while (reached(state.next)) {
        if (++work_ > 50000000) {
          return p::fail(kRuleInvalid, "replay operation limit exceeded");
        }
        if (!state.seen) {
          auto result = emit(state, *state.subject, {}, state.next, true);
          if (!result) {
            return result;
          }
        }
        state.seen = false;
        state.next += window_;
      }
    }
    for (auto it = state.pending.begin(); it != state.pending.end();) {
      if (reached(it->deadline)) {
        auto result = emit(state, *it->trigger, {it->trigger}, it->deadline, true);
        if (!result) {
          return result;
        }
        it = state.pending.erase(it);
      } else {
        ++it;
      }
    }
    return {};
  }

  p::Result<void> consume(State& state, const Event& event) {
    const auto now = occurred(event);
    if (rule_->rule.has_single()) {
      return emit(state, event, {&event}, now);
    }
    if (rule_->rule.has_absence()) {
      auto expected = matches(rule_->parts.front(), event, *rule_, *context_);
      if (!expected) {
        return std::unexpected(expected.error());
      }
      if (*expected) {
        state.pending.clear();
        state.seen = true;
      }
      if (rule_->trigger) {
        auto trigger = matches(*rule_->trigger, event, *rule_, *context_);
        if (!trigger) {
          return std::unexpected(trigger.error());
        }
        if (*trigger) {
          state.pending.push_back({&event, now + window_});
        }
      }
      return {};
    }
    while (!state.events.empty() && occurred(*state.events.front().event) <= now - window_) {
      state.events.pop_front();
    }
    std::uint32_t mask = 0;
    for (std::size_t i = 0; i < rule_->parts.size(); ++i) {
      auto part = matches(rule_->parts[i], event, *rule_, *context_);
      if (!part) {
        return std::unexpected(part.error());
      }
      if (*part) {
        mask |= 1U << i;
      }
    }
    state.events.push_back({&event, mask});
    std::vector<const Event*> chosen;
    if (rule_->rule.has_threshold()) {
      std::set<std::string> distinct;
      const auto& field = rule_->rule.threshold().distinct_by();
      for (const auto& candidate : state.events) {
        if (++work_ > 50000000) {
          return p::fail(kRuleInvalid, "replay operation limit exceeded");
        }
        std::string value = candidate.event->envelope().message_id();
        if (!field.empty()) {
          auto resolved = resolver(*candidate.event, *rule_, *context_, 1)("event." + field);
          if (!resolved || !std::holds_alternative<std::string>(*resolved)) {
            return p::fail(kRuleInvalid, "distinct field unavailable");
          }
          value = std::get<std::string>(*resolved);
        }
        if (distinct.insert(value).second) {
          chosen.push_back(candidate.event);
        }
        if (chosen.size() == rule_->rule.threshold().count()) {
          break;
        }
      }
      if (chosen.size() < rule_->rule.threshold().count()) {
        return {};
      }
    } else if (rule_->rule.has_sequence()) {
      std::size_t step = rule_->parts.size();
      std::int64_t next_time = std::numeric_limits<std::int64_t>::max();
      for (auto it = state.events.rbegin(); it != state.events.rend() && step > 0; ++it) {
        if (++work_ > 50000000) {
          return p::fail(kRuleInvalid, "replay operation limit exceeded");
        }
        if ((it->mask & (1U << (step - 1))) != 0 && occurred(*it->event) < next_time) {
          chosen.push_back(it->event);
          --step;
          next_time = occurred(*it->event);
        }
      }
      if (step != 0) {
        return {};
      }
    } else {
      std::vector<int> owner(state.events.size(), -1);
      bool exhausted = false;
      std::function<bool(std::size_t, std::vector<bool>&)> assign = [&](std::size_t part, std::vector<bool>& visited) {
        for (std::size_t i = 0; i < state.events.size(); ++i) {
          if (++work_ > 50000000) {
            exhausted = true;
            return false;
          }
          if (visited[i] || (state.events[i].mask & (1U << part)) == 0) {
            continue;
          }
          visited[i] = true;
          if (owner[i] < 0 || assign(static_cast<std::size_t>(owner[i]), visited)) {
            owner[i] = static_cast<int>(part);
            return true;
          }
        }
        return false;
      };
      for (std::size_t part = 0; part < rule_->parts.size(); ++part) {
        std::vector<bool> visited(state.events.size(), false);
        if (!assign(part, visited)) {
          if (exhausted) {
            return p::fail(kRuleInvalid, "replay operation limit exceeded");
          }
          return {};
        }
      }
      for (std::size_t i = 0; i < owner.size(); ++i) {
        if (owner[i] >= 0) {
          chosen.push_back(state.events[i].event);
        }
      }
    }
    auto result = emit(state, event, std::move(chosen), now);
    state.events.clear();
    return result;
  }

  const CompiledRule* rule_;
  const Context* context_;
  std::string tenant_;
  Limits limits_;
  std::int64_t window_;
  std::int64_t suppression_;
  std::map<std::string, State> states_{};
  ReplayResult result_{};
  std::size_t work_ = 0;
};
}  // namespace

p::Result<ReplayResult> replay(const CompiledRule& rule, std::vector<Event> events, const Context& context,
                               std::string tenant, std::int64_t from_ms, std::int64_t to_ms, Limits limits) {
  if (from_ms < 0 || to_ms > 253402300799999LL || from_ms >= to_ms || to_ms - from_ms > limits.max_window_ms
      || events.size() > limits.max_events || !p::stream::uuid(tenant)) {
    return p::fail(kRuleInvalid, "invalid replay interval, tenant or event limit");
  }
  for (const auto& policy : context.maintenance) {
    if (!policy.has_starts_at() || !policy.has_ends_at() || !Time::IsTimestampValid(policy.starts_at())
        || !Time::IsTimestampValid(policy.ends_at())
        || (policy.has_activated_at() && !Time::IsTimestampValid(policy.activated_at()))
        || (policy.has_updated_at() && !Time::IsTimestampValid(policy.updated_at()))
        || (policy.has_completed_at() && !Time::IsTimestampValid(policy.completed_at()))) {
      return p::fail(kRuleInvalid, "invalid maintenance history timestamp");
    }
  }
  std::map<std::string, std::string> identities;
  std::vector<Event> unique;
  std::size_t bytes = 0;
  for (auto& event : events) {
    if (event.envelope().tenant_id() != tenant) {
      continue;
    }
    if (!event.has_event() || !p::stream::uuid(event.envelope().message_id()) || !event.envelope().has_occurred_at()
        || !Time::IsTimestampValid(event.envelope().occurred_at())
        || (event.envelope().has_received_at() && !Time::IsTimestampValid(event.envelope().received_at()))) {
      return p::fail(kRuleInvalid, "invalid historical event");
    }
    bytes += event.ByteSizeLong();
    if (bytes > 16777216) {
      return p::fail(kRuleInvalid, "replay byte limit exceeded");
    }
    std::string wire;
    {
      google::protobuf::io::StringOutputStream stream(&wire);
      google::protobuf::io::CodedOutputStream coded(&stream);
      coded.SetSerializationDeterministic(true);
      if (!event.SerializeToCodedStream(&coded)) {
        return p::fail(kRuleInvalid, "historical event serialization failed");
      }
    }
    const auto fingerprint = p::stream::sha256(wire);
    auto [found, inserted] = identities.emplace(event.envelope().message_id(), fingerprint);
    if (!inserted) {
      if (found->second != fingerprint) {
        return p::fail(kRuleInvalid, "conflicting historical event identity");
      }
      continue;
    }
    unique.push_back(std::move(event));
  }
  std::ranges::sort(unique, [](const auto& lhs, const auto& rhs) {
    if (occurred(lhs) != occurred(rhs)) {
      return occurred(lhs) < occurred(rhs);
    }
    return lhs.envelope().message_id() < rhs.envelope().message_id();
  });
  return Replay(rule, context, std::move(tenant), limits).run(unique, from_ms, to_ms);
}
}  // namespace psim::correlation::rules
