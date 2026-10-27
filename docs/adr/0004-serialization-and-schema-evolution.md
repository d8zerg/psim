# ADR-004. Serialization format and schema evolution rules

<!-- language-links -->
[English](0004-serialization-and-schema-evolution.md) | [Русский](../ru/adr/0004-serialization-and-schema-evolution.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-003, ADR-005, ADR-026, [crosscutting.md](../architecture/crosscutting.md#10-contract-evolution)

## Context

Kafka messages, the connector protocol and REST require strict typed schemas, a compact format for 50,000 events/s, and evolution rules supporting zero-downtime upgrades while versions N and N+1 coexist.

## Considered options

### Format

| Option | Advantages | Disadvantages |
|---|---|---|
| **Protobuf (proto3)** | Compact and fast; one IDL for Kafka, gRPC and REST; mature C++ and TS tooling; `buf` linting and compatibility checks | No embedded message schema; addressed by the registry |
| Avro | Standard in the Kafka ecosystem | Weaker C++ tooling; a second IDL alongside gRPC |
| JSON + JSON Schema | Readability | Size and parsing cost at 50,000 events/s |
| FlatBuffers / Cap'n Proto | Zero-copy access | Limited registry and tooling support |

### Topic-message structure

| Option | Advantages | Disadvantages |
|---|---|---|
| A. Envelope with a `bytes` payload and type name | Flexibility | The registry sees only the envelope and cannot check payload compatibility |
| B. Envelope in Kafka headers, payload in the body | The registry checks the body | The envelope is untyped and unchecked; headers can be lost during transfer |
| **C. One record per topic: `<Topic>Record { Envelope envelope = 1; oneof payload {...} }`** | The envelope and every topic event type are typed and checked as one schema; a new event type adds a `oneof` branch | Every event type for a topic belongs to one record file |

## Decision

1. **Protobuf proto3** is the sole contract IDL. Fields requiring explicit presence use `optional`.
2. **Structure C:** a topic message value is a topic record, such as `psim.incident.v1.IncidentEventRecord`, with an envelope field and `oneof payload`. Use `TopicNameStrategy` for subjects (`<topic>-value`).
3. **Wire format:** Confluent framing with magic byte `0`, four schema-ID bytes, Protobuf message indexes and the body. The message key is a UTF-8 string without a schema.
4. **Compatibility:** `BACKWARD_TRANSITIVE` for topic records and `FULL_TRANSITIVE` for the envelope and shared types. Compile-time verification uses `buf breaking` with `WIRE_JSON` rules against the latest release.
5. **Evolution rules:**
   - Add only optional fields with new numbers.
   - Mark removed field names and numbers as `reserved`.
   - Never change a field's type or meaning; introduce a new field for new meaning.
   - Add a new event type as a new `oneof` branch. Consumers must ignore unknown branches, logging and counting them without sending them to DLQ.
   - Enum value `0` is `*_UNSPECIFIED`; unknown values are handled as unknown.
   - An incompatible change requires a new package version (`v2`) and topic (`.v2`), with a dual-write period.
6. **Package naming:** `psim.<context>.v<N>`; files are `contracts/proto/psim/<context>/v<N>/*.proto`.

## Consequences

- One code-generation approach for C++ with protoc + gRPC and TypeScript under ADR-026.
- The Kafka layer implements the Confluent wire codec, including Protobuf message indexes.
- Consumers tolerate future event types without a new release.

## Verification

Run `buf lint` and `buf breaking` in CI, the registry contract test from ADR-003, and an old-consumer/new-producer test in the upgrade suite.
