# ADR-006. Delivery semantics by data flow

<!-- language-links -->
[English](0006-delivery-semantics.md) | [Русский](../ru/adr/0006-delivery-semantics.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-002, ADR-005, ADR-007, ADR-008, [data-flows.md](../architecture/data-flows.md)

## Context

Goals are zero lost events or incidents and zero duplicate incidents under failures and retries, as required by S5, S11 and QG1, with event -> signal p99 at most 300 ms. Every pipeline stage needs explicit semantics and an idempotency mechanism.

## Considered options

### Normalization

| Option | Advantages | Disadvantages |
|---|---|---|
| A. At-least-once, consumers deduplicate by `event_id` | Minimum stage latency | Duplicates after Normalizer failure reach correlation and distort threshold rules. Correlation Engine deduplication needs an `event_id` set covering minutes: tens of millions of identifiers at 50,000 events/s |
| **B. Kafka exactly-once transactions, with source-sequence tracking in a compacted topic committed in the same transaction** | Duplicate-free normalized stream; centralized deduplication with small per-source state, a maximum and bitmap | Transaction commit intervals consume part of the stage's 30 ms budget |

### PostgreSQL boundary

| Option | Advantages | Disadvantages |
|---|---|---|
| Distributed Kafka + PostgreSQL transaction, 2PC | Atomicity | Kafka does not participate in XA; complexity and fragility |
| **Inbox + outbox** | Atomicity within a database transaction; established pattern | Outbox publication can repeat, requiring idempotent consumers |

## Decision

Semantic classes:

| Class | Mechanism | Flows |
|---|---|---|
| **EOS-K** | Kafka read-process-write transaction atomically commits output, changelog and input offsets. Each worker has a unique `transactional.id`; KIP-447 binds offset commits to the group generation; consumers use `read_committed` | `psim.events.normalized.v1` in Normalizer, `psim.signals.v1` and `psim.correlation.*` in Correlation Engine |
| **EOS-DB** | Inbox insertion in the aggregate-change transaction; commit the offset after database commit | Input to Incident Service, Response Engine, Command Service and Audit Service |
| **Outbox** | Persist an event in the aggregate transaction and publish through a relay. Retries are possible; consumers apply by `version` or inbox | All domain events from PostgreSQL-backed services |
| **ALO+ID** | At-least-once with identifier-based idempotency | Connector ingestion, deduplicated in Normalizer; commands, deduplicated by SDK `command_id`; command results; audit by `source_event_id`; projections by `version` |

Specific decisions:

1. Normalizer uses EOS-K, option B. [ADR-052](0052-normalization-and-event-history.md) specifies recovery, cursor commits and partition ownership transfer. If end-to-end measurements show the stage budget is exceeded, the fallback is option A with Correlation Engine `event_id` deduplication over the Normalizer offset-commit interval multiplied by a safety factor.
2. Acknowledge connectors only after Kafka confirms the write with `acks=all`.
3. Publish command and notification lifecycles in `psim.response.events.v1`, keyed by incident, rather than separate topics. The console and projections receive one ordered stream per incident.
4. Transient errors such as storage unavailability pause the partition and retry with exponential backoff. DLQ is reserved for permanent message errors.
5. Transaction commit intervals are at most 50 ms in Correlation Engine and 20 ms in Normalizer, subject to measurement.

## Consequences

- Failure-injection simulation establishes each stage's correctness, including termination at any point, retries and reordering.
- EOS-K stage latency includes the commit interval, which the [latency budget](../architecture/quality.md#4-delay-budget) accounts for.
- Every outbox-topic consumer must be idempotent. The platform library provides inbox and version filtering.

## Verification

Reconcile `source_seq` and incident counts end to end in load and chaos tests. Run 10,000 simulation executions in the nightly build.
