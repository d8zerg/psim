# ADR-029. Deterministic source identifiers

<!-- language-links -->
[English](0029-deterministic-source-id.md) | [Русский](../ru/adr/0029-deterministic-source-id.md)
<!-- /language-links -->

- **Status:** Accepted
- **Extends:** [ADR-005](0005-envelope-and-identifiers.md)
- **Related:** ADR-007, ADR-017

## Context

Connectors identify sources by `source_ref`, equal to catalog `Source.external_ref`, while `psim.ingest.raw.v1` uses `source_id` as its key (ADR-007). S1 and S3 require events from unregistered sources to reach the Normalizer DLQ instead of being rejected by the gateway: an administrator registers the device and replays the DLQ. With catalog-assigned UUIDv7 `source_id` values, the gateway cannot calculate an unregistered source's key, and registration changes the event key.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Gateway rejects unregistered-source events | Simple | Violates S1 and S3 DLQ behavior; connectors must handle rejected events |
| Use `connector_id:source_ref` instead of `source_id` | Needs no catalog | Changes ADR-007; topics use different keys |
| **`source_id` = UUIDv5(`connector_id`, `source_ref`)** | Gateway calculates it without catalog access; registration preserves it; another connector's source cannot match by construction | Moving a source to another connector changes its identifier |

## Decision

1. `source_id` is UUIDv5 in the source namespace from `contracts/`, using a name composed from `connector_id` and `source_ref` according to ADR-005.
2. The catalog computes `source_id` identically when registering a device; manual assignment is forbidden.
3. The gateway computes `source_id` for every event and writes it to `RawEvent` with `source_ref`. CS3, source ownership by the session's connector, holds by construction.
4. Moving a source to another connector creates a new source with a new `source_id`; the old source's history remains.

## Consequences

- The [identifier table](../domain/aggregates.md#01-identifiers) classifies `source_id` as a name-based UUID.
- Normalizer routes unregistered-source events to the DLQ with `PROCESSING_DEVICE_NOT_FOUND`. After registration, `psimctl dlq replay` reprocesses them.

## Verification

Identical `connector_id` and `source_ref` values produce the same `source_id` in gateway and catalog. An unregistered-source event reaches the DLQ and normalizes after registration and replay.
