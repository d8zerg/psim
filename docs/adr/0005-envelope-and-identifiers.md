# ADR-005. Message envelope, UUIDv7 and named UUIDs

<!-- language-links -->
[English](0005-envelope-and-identifiers.md) | [Русский](../ru/adr/0005-envelope-and-identifiers.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-004, ADR-006, ADR-029, [aggregates.md](../domain/aggregates.md#01-identifiers), [crosscutting.md](../architecture/crosscutting.md#1-message-envelope)

## Context

Every message needs common metadata for deduplication, tracing, multitenancy and latency measurement. Identifiers must sort by time for PostgreSQL indexes and debugging. Some entities also need deterministic identifiers so that reprocessing produces the same ID and duplicates are rejected.

## Considered options

### Identifiers

| Option | Advantages | Disadvantages |
|---|---|---|
| UUIDv4 | Universal support | Random ordering fragments B-tree indexes |
| **UUIDv7 (RFC 9562)** | Time-monotonic, good index locality, 128 bits | Reveals creation time, which is acceptable |
| ULID / Snowflake | Sortable | Nonstandard format or coordinated node identifiers |
| Database sequences | Compact | Require a database during creation; unsuitable for SDK and Kafka |

### Deterministic identifiers

| Option | Advantages | Disadvantages |
|---|---|---|
| **UUIDv5 (SHA-1, RFC 9562)** | Standard and available in Boost.Uuid; SHA-1 collision resistance is sufficient for internal keys not exposed to adversarial construction | SHA-1 is unsuitable for security, which is not required here |
| UUIDv8 with SHA-256 | Modern hash | Nonstandard contents and custom implementations in every language |
| Composite natural key | No hashing | Variable length and shape; inconvenient for external systems |

## Decision

1. **UUIDv7** for newly created entities and events except those in item 2. Generate through the `IdGenerator` port; a counter in lower bits provides process-local monotonicity using RFC 9562 method 1.
2. **UUIDv5** for `source_id` under ADR-029, `signal_id`, `run_id`, response-plan `command_id` and `notification_id`. Each identifier kind has a separate fixed UUID namespace stored in `contracts/`. The name is a canonical component serialization: every component is encoded as a varint length followed by bytes, preventing ambiguous concatenation.
3. **Envelope** `psim.common.v1.Envelope`:

| Field | Type | Required |
|---|---|---|
| `message_id` | string (UUID) | yes |
| `tenant_id` | string (UUID) | yes |
| `occurred_at` | `google.protobuf.Timestamp` | yes |
| `produced_at` | `google.protobuf.Timestamp` | yes |
| `received_at` | `google.protobuf.Timestamp` | for ingested events |
| `sequence` | uint64 | source sequence number or aggregate version |
| `causation_id` | string (UUID) | for derived messages |
| `correlation_id` | string (UUID) | yes; equals `message_id` for an original event |
| `actor` | `Actor { kind: USER/SERVICE/CONNECTOR/SYSTEM; id }` | yes |
| `schema_version` | string | contract version that produced the message, for diagnostics |

The topic record's `oneof` branch determines payload type under ADR-004, so no separate type field is needed. W3C trace context travels in Kafka `traceparent`/`tracestate` headers rather than the envelope, allowing standard tools to preserve it.

4. UUIDs are canonical 36-character strings in Protobuf for REST and connectors, and strings in topics, ensuring consistent JSON mapping and tool readability. Optimization to `bytes`, or 16 bytes, requires a separate decision based on load measurements.

## Consequences

- The platform layer provides v7 and v5 generators and format validation.
- Pipeline-wide deduplication relies on `message_id` and deterministic identifiers.
- String UUIDs add approximately 20 bytes per identifier. Sizing accounts for this, and zstd compression reduces the impact.

## Verification

Generator unit tests use RFC 9562 vectors. Property tests establish identical inputs -> identical identifiers. The contract linter checks for an envelope in every topic record.
