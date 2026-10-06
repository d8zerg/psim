// Cost of the contract envelope on the hot path: every Kafka record is serialized once by its
// producer and parsed by each consumer (ADR-004, ADR-005). Baseline for the regression gate (B-05).

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "psim/common/v1/envelope.pb.h"
#include "psim/incident/v1/events.pb.h"
#include "psim/incident/v1/incident.pb.h"

namespace {

psim::incident::v1::IncidentEventRecord make_record() {
  psim::incident::v1::IncidentEventRecord record;
  auto* envelope = record.mutable_envelope();
  envelope->set_message_id("01928f5e-7a3b-7c4d-8e9f-0a1b2c3d4e5f");
  envelope->set_tenant_id("01928f5e-0000-7000-8000-000000000001");
  envelope->mutable_occurred_at()->set_seconds(1'790'000'000);
  envelope->mutable_produced_at()->set_seconds(1'790'000'001);
  envelope->set_sequence(7);
  envelope->set_correlation_id(envelope->message_id());
  envelope->mutable_actor()->set_kind(psim::common::v1::ACTOR_KIND_SERVICE);
  envelope->mutable_actor()->set_id("incident-service");
  auto* incident = record.mutable_incident_created()->mutable_incident();
  incident->set_incident_id("01928f5e-7a3b-7c4d-8e9f-000000000042");
  incident->set_state(psim::incident::v1::INCIDENT_STATE_NEW);
  incident->set_version(1);
  return record;
}

void incident_record_serialize(benchmark::State& state) {
  const auto record = make_record();
  std::string wire;
  for ([[maybe_unused]] auto iteration : state) {
    wire.clear();
    record.SerializeToString(&wire);
    benchmark::DoNotOptimize(wire.data());
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(wire.size()));
}

void incident_record_parse(benchmark::State& state) {
  const std::string wire = make_record().SerializeAsString();
  psim::incident::v1::IncidentEventRecord record;
  for ([[maybe_unused]] auto iteration : state) {
    benchmark::DoNotOptimize(record.ParseFromString(wire));
  }
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(wire.size()));
}

}  // namespace

BENCHMARK(incident_record_serialize);
BENCHMARK(incident_record_parse);
