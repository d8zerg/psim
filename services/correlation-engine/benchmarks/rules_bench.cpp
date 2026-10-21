#include <benchmark/benchmark.h>
#include <google/protobuf/util/time_util.h>
#include <psim/common/v1/types.pb.h>
#include <psim/processing/v1/rules.pb.h>

#include <cstdint>
#include <stdexcept>
#include <string>

#include "live.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "rules.hpp"

namespace psim::correlation::live {
namespace {
using Time = google::protobuf::util::TimeUtil;
constexpr auto kTenant = "11111111-1111-4111-8111-111111111111";
constexpr auto kZone = "33333333-3333-4333-8333-333333333333";

void rule_evaluation(benchmark::State& benchmark_state) {
  rules::Rule spec;
  spec.set_rule_id(kZone);
  spec.set_version(1);
  spec.set_state(processing::v1::RULE_STATE_ACTIVE);
  spec.set_name("Perimeter");
  spec.set_correlation_scope(processing::v1::CORRELATION_SCOPE_ZONE);
  spec.mutable_window()->set_seconds(1);
  spec.mutable_allowed_lateness()->set_seconds(0);
  spec.mutable_filter()->add_types("intrusion.*");
  spec.mutable_filter()->set_condition("event.severity >= 3 && event.type.startsWith('intrusion.')");
  spec.mutable_output()->set_incident_type_id(kZone);
  spec.mutable_output()->set_priority(common::v1::PRIORITY_P2);
  spec.mutable_output()->set_priority_expression("event.severity >= 4 ? 2 : 3");
  spec.mutable_output()->set_summary_template("${event.zone_id} / ${rule.name}");
  const auto kind = benchmark_state.range(0);
  if (kind == 0) {
    spec.mutable_single();
  } else if (kind == 1 || kind == 2) {
    auto* first = kind == 1 ? spec.mutable_conjunction()->add_parts() : spec.mutable_sequence()->add_steps();
    first->add_types("intrusion.zone.alarm");
    auto* second = kind == 1 ? spec.mutable_conjunction()->add_parts() : spec.mutable_sequence()->add_steps();
    second->add_types("intrusion.zone.restore");
  } else if (kind == 3) {
    spec.mutable_threshold()->set_count(2);
  } else {
    spec.mutable_absence()->mutable_trigger()->add_types("intrusion.zone.alarm");
    spec.mutable_absence()->mutable_expected()->add_types("intrusion.zone.restore");
  }
  auto validation = rules::compile(spec, {"intrusion.zone.alarm", "intrusion.zone.restore"});
  if (!validation.compiled) {
    throw std::runtime_error("invalid benchmark rule");
  }
  const auto& rule = *validation.compiled;
  rules::Event event;
  event.mutable_envelope()->set_tenant_id(kTenant);
  auto* value = event.mutable_event();
  value->set_site_id(kTenant);
  value->set_zone_id(kZone);
  value->set_device_id(kZone);
  value->set_source_id(kZone);
  value->set_source_epoch(1);
  value->set_severity(common::v1::SEVERITY_HIGH);
  State state;
  *state.mutable_rule() = spec;
  std::uint64_t sequence = 0;
  std::int64_t at = 1000;
  for (auto unused : benchmark_state) {
    (void)unused;
    for (const bool second : {false, true}) {
      const auto gap = kind == 4 ? 2000 : 1;
      const auto time = at + (second ? gap : 0);
      event.mutable_envelope()->set_sequence(++sequence);
      event.mutable_envelope()->set_message_id(p::stream::uuid5(kTenant, {std::to_string(sequence)}));
      *event.mutable_envelope()->mutable_occurred_at() = Time::MillisecondsToTimestamp(time);
      *event.mutable_envelope()->mutable_received_at() = Time::MillisecondsToTimestamp(time);
      event.mutable_event()->set_type(second ? "intrusion.zone.restore" : "intrusion.zone.alarm");
      auto evaluated = advance(rule, state, &event, {}, kTenant, time);
      if (!evaluated) {
        benchmark_state.SkipWithError(evaluated.error().message());
        return;
      }
      benchmark::DoNotOptimize(evaluated->decisions.size());
    }
    at += kind == 4 ? 4000 : 2;
  }
  benchmark_state.SetItemsProcessed(benchmark_state.iterations() * 2);
}

BENCHMARK(rule_evaluation)->Arg(0)->Arg(1)->Arg(2)->Arg(3)->Arg(4)->ArgName("kind");
}  // namespace
}  // namespace psim::correlation::live
