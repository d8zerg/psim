# ADR-013. MVP event-history storage and long-term archive

<!-- language-links -->
[English](0013-event-history-storage.md) | [Русский](../ru/adr/0013-event-history-storage.md)
<!-- /language-links -->

- **Status:** Superseded by [ADR-028](0028-clickhouse-analytical-storage.md)
- **Related:** [scaling.md](../architecture/scaling.md#5-capacity-planning), X8 in [mvp-scope.md](../product/mvp-scope.md#2-beyond-mvp)

## Context

Operators and auditors need event searches by resource, zone, device, type and time, plus event-to-incident links. A one-day search by zone must complete within 500 ms under full write load. Long-term storage of all 50,000 events/s is expensive: approximately 4.3 billion rows per day.

## Considered options

| Option | Advantages | Disadvantages |
|---|---|---|
| All events in PostgreSQL | Simple queries | Volume and write load are disproportionate to the MVP |
| **Significant events in PostgreSQL with daily partitions; all events in Kafka within retention** | 20x lower volume at 5% significant events; familiar storage; fast indexed search within a partition | Other events are available only within Kafka retention |
| Columnar storage, such as ClickHouse, for all events | Analytics over the entire stream | Additional deployment and operational subsystem; analytics outside the MVP |
| OpenSearch / Elasticsearch | Full-text search | Operational burden and differing licenses; full-text search is unnecessary |

## Decision

1. **Event History** writes significant events to PostgreSQL when `severity ≥` the tenant threshold, default `medium`, together with every event referenced by signals consumed from `psim.signals.v1`.
2. Partition the table by `occurred_at` day, with indexes on `(tenant_id, zone_id, occurred_at)`, `(tenant_id, device_id, occurred_at)` and `(tenant_id, type, occurred_at)`. Insert batches through `COPY`; enforce per-partition idempotency with `ON CONFLICT (event_id) DO NOTHING`.
3. Default history retention is 30 days, with deletion by detaching partitions.
4. A long-term analytical archive of all events is outside the MVP. A candidate is a columnar store consuming `psim.events.normalized.v1`; add a consumer group without changing the pipeline.

## Consequences

- The significance threshold determines storage volume; sizing follows load measurements.
- Non-significant events older than Kafka retention, default 24 hours, cannot be recovered. Document this for customers.

## Verification

Load test: a one-day zone search completes within 500 ms while inserting 2500 rows/s.
