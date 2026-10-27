# ADR-057. Incident Facts and Reporting

<!-- language-links -->
[English](0057-incident-reporting.md) | [Русский](../ru/adr/0057-incident-reporting.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-028](0028-clickhouse-analytical-storage.md), [ADR-045](0045-clickhouse-layer.md), [ADR-056](0056-sla-and-notifications.md), [ADR-020](0020-authorization-model.md)

## Context

Reporting must count incidents and response metrics regardless of Kafka redelivery, SLA policy changes, responsibility transfers, and reopening. PostgreSQL stores working aggregates; ClickHouse handles analytics. Regular summarization in a materialized view yields incorrect results after repeated delivery or aggregate changes.

## Solution

1. The Projection Service loads `psim.incidents.events.v1` and `psim.response.events.v1` through a shared Kafka -> ClickHouse batch journal. Damaged records go to `psim.dlq.projection.v1`; correct events from other functions are skipped with position retention without DLQ.
2. `incident_facts` contains typed incident snapshots and SLA source values. Retention is 400 days from incident creation. `ReplicatedReplacingMergeTree` uses the aggregate version, while the materialized view stores `argMaxState` in `ReplicatedAggregatingMergeTree`. Reading uses `argMaxMerge`, so results are correct before background merging and after repeated recomputation. All tables are created by `ON CLUSTER`.
3. The Response Engine atomically publishes a compatible event `SlaPolicyPinned` when an incident SLA is first persisted. It contains original acknowledgement and resolution durations. Policy and priority changes do not alter them. During recomputation, old incidents receive values from the saved policy and original priority in `escalation.incidents`.
4. The reporting interval is half-open, with millisecond accuracy. Created incidents are counted by creation time, MTTA by acceptance time, resolved, MTTR, and false alarms by resolution time. An incident can be created before the period starts. Daily grouping uses corresponding observation time. The latest incident state is used: reopening excludes previous resolutions, transfer changes the responsible group. Load is the number of incidents for the last responsible person, not actions per operator.
5. MTTA is the average time from creation to acceptance among accepted incidents. MTTR is the average time from creation to current resolution among resolved incidents. False alarms are divided by the number resolved in a period, according to S13. SLA acceptance counts from creation, SLA resolution from acceptance, according to the execution model. Denominators include completed or overdue observations with known source values; pending and incidents without SLA are excluded. SLA observation relates to completion or deadline, depending on which occurred first; denominator sizes are returned explicitly. Empty denominators give zero.
6. One ClickHouse query builds groups and totals through `ROLLUP`; averages and percentages are counted by incidents. Filters and access scope are passed as native parameters. Roles `manager`, `supervisor` and `auditor` have `report.read` and `report.export`; administrator status alone does not grant access. Allowed operations and failures are recorded through outbox.
7. Recomputation reads PostgreSQL in a transaction `REPEATABLE READ READ ONLY`, in limited batches, and inserts snapshots without deleting target data. It is compatible with running loaders: older versions do not replace new ones. If the SLA falls behind the Incident Service, re-calculation ends with a retryable error. The tool does not change working aggregates or Kafka positions.

## Consequences

- Reporting reflects the last delivered state, so Kafka delays affect freshness. Reporting is not a snapshot of historical state at the end of the selected period.
- Fixing data with the same version is not supported. The source must increase the version and save creation time.
- Tables serve local shards. Distributed tables for multiple shards and cluster checks are part of the operational topology.
- Losing the materialized state requires re-calculation from PostgreSQL. Facts and their aggregates are restored together; reinserting already deduplicated blocks does not restore the deleted view.
- The aggregate state mechanism follows [ClickHouse documentation](https://clickhouse.com/blog/using-materialized-views-in-clickhouse).

## Verification

`task reports:test` checks known metrics, two service copies, repetitions, older versions, damaged input, roles, access scope, CSV, SIGKILL, and re-calculation. `task reports:bench` checks reporting over 30 days for one million incidents. Sanitizers and coverage are run as separate tasks.
