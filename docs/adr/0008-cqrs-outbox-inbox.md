# ADR-008. CQRS, transactional outbox and inbox

<!-- language-links -->
[English](0008-cqrs-outbox-inbox.md) | [Русский](../ru/adr/0008-cqrs-outbox-inbox.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-006, ADR-019, [crosscutting.md](../architecture/crosscutting.md#3-transaction-patterns)

## Context

Services whose aggregates reside in PostgreSQL, including catalog, incidents, response plans, commands, notifications and audit, must publish domain events to Kafka atomically with aggregate changes and consume Kafka messages without applying them twice. The console needs fast read models for a 10,000-incident feed and resource state without loading aggregates.

## Considered options

### Event publication

| Option | Advantages | Disadvantages |
|---|---|---|
| Publish to Kafka after database commit | Simple | An event can be lost if the process fails between commit and publication |
| CDC through Debezium | WAL-based, without relay code | JVM and Kafka Connect add a deployment subsystem; latency; event format coupled to tables |
| Custom logical WAL decoding | Minimum latency | Complex replication protocol, replication slots and their operation |
| **Outbox table and polling relay, with `LISTEN/NOTIFY` wakeup** | Simple, reliable implementation; domain events rather than table rows; latency of tens of milliseconds | Polling load, mitigated by NOTIFY and batching |

### Read models

| Option | Advantages | Disadvantages |
|---|---|---|
| Read aggregate tables directly | No separate models | Expensive feed queries compete with writes; no Kafka-based recovery |
| **CQRS: Projection Service builds read models from topics** | Independent read scaling; replay-based rebuilding | Eventual consistency, addressed by snapshots with offsets under ADR-019 |

## Decision

1. **Outbox:** each service schema has a `psim_outbox` table written in the aggregate transaction. The relay polls every 100 ms by default. A transaction-level advisory lock selects one publisher per schema. Batches are selected by `sequence` with row-count and size limits. After Kafka acknowledges publication of the entire batch, the relay sets `published_at` and commits the transaction. Errors leave records available for retry.
2. **Inbox:** each service schema has a `psim_inbox` table keyed by `(consumer, event_id)`, with a content fingerprint. Insert in the aggregate-change transaction. Skip a retry with identical content; reject different content with the same identifier. Inbox/outbox rows are not automatically deleted. Retention policy must cover the maximum redelivery window.
3. **Aggregate versions:** every domain event carries `version`; projection consumers apply an event only when its version exceeds the applied version.
4. **CQRS:** send commands to the aggregate-owning service. Console queries go to Projection Service for the feed, resource state and report aggregates, or to services for individual aggregate details. Rebuild projections by replaying topics with `psimctl projection console rebuild`.
5. `libs/platform/pg` implements outbox, relay and inbox once for all services.

## Consequences

- Outbox publication can repeat, so every consumer must be idempotent under ADR-006.
- Source-topic retention limits rebuilding from scratch. Incomplete history is rejected and the working projection is preserved. Complete PostgreSQL loss requires sufficient Kafka history or an independent backup; see [console projections](../engineering/console-projections.md).

## Verification

Test the outbox library with process failures before commit, after commit, after publication and before committing the publication marker. Verify that a rebuilt projection is identical to the working model, as required by QS-A6.
