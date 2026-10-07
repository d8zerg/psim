// Cost of the Confluent framing on the hot path (B-05 baseline of the Kafka layer).

#include <benchmark/benchmark.h>

#include <string>

#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/kafka/wire_format.hpp"

namespace {

using psim::ingest::v1::RawEventRecord;
using psim::platform::kafka::ProtobufSerde;

RawEventRecord sample() {
  RawEventRecord record;
  auto* envelope = record.mutable_envelope();
  envelope->set_message_id("0190f1a2-0000-7000-8000-000000000001");
  envelope->set_tenant_id("0190f1a2-0000-7000-8000-000000000002");
  envelope->set_correlation_id(envelope->message_id());
  envelope->set_sequence(42);
  auto* event = record.mutable_raw_event();
  event->set_connector_id("0190f1a2-0000-7000-8000-000000000003");
  event->set_source_id("0190f1a2-0000-7000-8000-000000000004");
  event->set_source_ref("door-17");
  event->set_raw_code("DOOR_FORCED");
  (*event->mutable_attributes())["zone"] = "north";
  return record;
}

void serde_serialize(benchmark::State& state) {
  const ProtobufSerde<RawEventRecord> serde(17);
  const auto record = sample();
  for ([[maybe_unused]] auto iteration : state) {
    benchmark::DoNotOptimize(serde.serialize(record));
  }
}

void serde_deserialize(benchmark::State& state) {
  const ProtobufSerde<RawEventRecord> serde(17);
  const std::string value = serde.serialize(sample());
  for ([[maybe_unused]] auto iteration : state) {
    benchmark::DoNotOptimize(serde.deserialize(value));
  }
}

void wire_parse(benchmark::State& state) {
  const ProtobufSerde<RawEventRecord> serde(17);
  const std::string value = serde.serialize(sample());
  for ([[maybe_unused]] auto iteration : state) {
    benchmark::DoNotOptimize(psim::platform::kafka::wire::parse(value));
  }
}

}  // namespace

BENCHMARK(serde_serialize);
BENCHMARK(serde_deserialize);
BENCHMARK(wire_parse);
