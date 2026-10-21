#pragma once

#include <google/protobuf/util/time_util.h>

#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "live.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::correlation::live::test {
using Time = google::protobuf::util::TimeUtil;
inline constexpr auto kTenant = "11111111-1111-4111-8111-111111111111";
inline constexpr auto kSite = "22222222-2222-4222-8222-222222222222";
inline constexpr auto kZone = "33333333-3333-4333-8333-333333333333";
inline constexpr auto kRule = "44444444-4444-4444-8444-444444444444";

inline rules::Rule rule() {
  rules::Rule value;
  value.set_rule_id(kRule);
  value.set_version(1);
  value.set_name("Perimeter");
  value.set_state(processing::v1::RULE_STATE_ACTIVE);
  value.set_correlation_scope(processing::v1::CORRELATION_SCOPE_ZONE);
  value.mutable_window()->set_seconds(1);
  value.mutable_filter()->add_types("intrusion.*");
  value.mutable_single();
  value.mutable_output()->set_incident_type_id(kRule);
  value.mutable_output()->set_priority(common::v1::PRIORITY_P2);
  return value;
}

inline rules::CompiledRule compile(const rules::Rule& value) {
  auto checked = rules::compile(value, {"intrusion.zone.alarm", "intrusion.zone.restore", "fire.detector.alarm"});
  if (!checked.compiled) {
    throw std::runtime_error("invalid live test rule");
  }
  return std::move(*checked.compiled);
}

inline rules::Event event(std::int64_t at, std::uint64_t sequence = 1, std::string type = "intrusion.zone.alarm") {
  rules::Event value;
  auto* envelope = value.mutable_envelope();
  envelope->set_tenant_id(kTenant);
  envelope->set_message_id(p::stream::uuid5(kTenant, {std::to_string(at), std::to_string(sequence), type}));
  envelope->set_sequence(sequence);
  *envelope->mutable_occurred_at() = Time::MillisecondsToTimestamp(at);
  *envelope->mutable_received_at() = Time::MillisecondsToTimestamp(at + 100);
  auto* output = value.mutable_event();
  output->set_type(std::move(type));
  output->set_source_id(kZone);
  output->set_device_id(kZone);
  output->set_site_id(kSite);
  output->set_zone_id(kZone);
  output->set_source_epoch(1);
  return value;
}
}  // namespace psim::correlation::live::test
