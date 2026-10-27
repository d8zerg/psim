# ADR-028. ClickHouse for event history and analytics

<!-- language-links -->
[English](0028-clickhouse-analytical-storage.md) | [Русский](../ru/adr/0028-clickhouse-analytical-storage.md)
<!-- /language-links -->

- **Status:** Accepted
- **Supersedes:** [ADR-013](0013-event-history-storage.md)
- **Related:** ADR-003, ADR-008, ADR-022, [scaling.md](../architecture/scaling.md#5-capacity-planning)

## Context

ClickHouse is selected for suitable data and workloads. ADR-013 stored only significant events, 5% of the stream, in PostgreSQL; the complete stream remained in Kafka only for its 24 h retention, with long-term archives outside MVP. At 50,000 events/s, approximately 4.3 billion per day, reports and incident investigation require historical search and aggregates covering 30 days or more.

The decision must assign data and workloads to ClickHouse, PostgreSQL, Redis and RocksDB.

## Options considered

### Data stored in ClickHouse

| Data and workload | ClickHouse suitability | Decision |
|---|---|---|
| All normalized events; search by site, zone, device, type and time | Suitable: batch inserts, 10-15× column compression, search-key ordering and TTL | ClickHouse |
| Event relationships to signals and incidents | Suitable: append-only relationship tables | ClickHouse |
| Incident facts for MTTA, MTTR, false-alarm, SLA and operator-load reports | Suitable: subsecond aggregates over long periods | ClickHouse |
| Signal and rule statistics for rule tuning | Suitable | ClickHouse |
| Domain aggregates: incidents, scenarios, commands, catalog, outbox and inbox | Unsuitable: transactions, uniqueness and point updates are required | PostgreSQL |
| Hash-chain audit log | Unsuitable: strict sequencing and database-enforced mutation restrictions are required (ADR-021) | PostgreSQL |
| Hot console projections, sessions and limits | Unsuitable: low-latency point reads and writes are required | Redis-compatible store |
| Correlation state | Unsuitable: local partition state with exactly-once semantics (ADR-009) | RocksDB |
| Platform logs and traces | Suitable through the OpenTelemetry Collector ClickHouse exporter | Rejected for the distribution; separate Loki and Tempo per [ADR-080](0080-observability-package.md) |

### Ingestion

| Option | Advantages | Disadvantages |
|---|---|---|
| ClickHouse Kafka table engine | No separate service | No direct Confluent Protobuf wire-format support; error and DLQ handling do not follow platform rules; metrics and tracing are outside the platform |
| **C++ consumer service with native-protocol batch inserts**, using Apache 2.0 clickhouse-cpp | Shared consumption, DLQ, metrics and tracing rules (ADR-006); controlled batch size | A small custom ingestion implementation |

### Topology

| Option | Advantages | Disadvantages |
|---|---|---|
| **Replicated engines and ClickHouse Keeper in every topology**: one ClickHouse node and one Keeper on a single machine; one shard with two replicas and three Keepers on infrastructure nodes in a cluster | Identical DDL across topologies; cluster fault tolerance | Adds Keeper |
| Nonreplicated MergeTree on one machine, Replicated in a cluster | Simpler single-machine deployment | Two DDL and migration variants |

## Decision

1. **ClickHouse**, Apache 2.0 with an LTS version pinned in the distribution, stores:
   - `events`: all normalized events;
   - `event_signals`: event-to-signal relationships from `psim.signals.v1`;
   - `incident_facts`: lifecycle facts from `psim.incidents.events.v1` and `psim.response.events.v1`;
   - `signal_facts`: signals for rule statistics.
2. **Ingestion:** Event History handles events, relationships and signals; Projection Service handles incident facts. Both are C++ Kafka consumers using native-protocol batch inserts over TLS. Offsets commit after successful insertion. Recovery duplicates are handled by `insert_deduplication_token`, derived from topic, partition and offset range, with `ReplacingMergeTree` as a fallback.
3. **`events` model**, refined by ADR-052: `ReplicatedReplacingMergeTree`, `PARTITION BY toYYYYMMDD(occurred_at)`, `ORDER BY (tenant_id, site_id, zone_id, occurred_at, event_id)`; skipping indexes on `device_id`, `type` and `event_id`; `type` and `raw_code` use `LowCardinality`. Lookup by `event_id` is bounded by the requested interval, defaulting to the latest 90 days.
4. **Retention:** TTL defaults are 7 days for all events, 90 days for significant events and relationships, and 400 days for incident facts. Configure them through `RetentionSettings` in `psim.config.v1`.
5. **Topology:** Replicated engines and ClickHouse Keeper everywhere; one node and one Keeper on a single machine; one shard, two replicas and three Keepers on cluster infrastructure nodes.
6. **Reads:** EventHistoryService and ReportService query ClickHouse. Every query is bounded by `tenant_id` (ADR-022) and a time interval.
7. ClickHouse is not the domain source of truth. Tables can be rebuilt by replaying Kafka within topic retention or recomputing incident facts from PostgreSQL.

## Consequences

- Complete event history for investigation becomes part of MVP. Out-of-scope item X8 is narrowed to BI tools and arbitrary analytics.
- History storage at 50,000 events/s decreases compared with ADR-013 despite retaining all events ([scaling.md](../architecture/scaling.md#5-capacity-planning)).
- Distribution installation, monitoring and backups now include ClickHouse and ClickHouse Keeper.
- ClickHouse migrations follow expand/contract rules (ADR-024).

## Verification

Load test: insert 50,000 events/s, search one day by zone within 500 ms under full write load, and generate a 30-day report within 3 s.
