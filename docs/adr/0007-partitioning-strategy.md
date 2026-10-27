# ADR-007. Partitioning strategy and high-load keys

<!-- language-links -->
[English](0007-partitioning-strategy.md) | [Русский](../ru/adr/0007-partitioning-strategy.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-006, ADR-009, [scaling.md](../architecture/scaling.md), [data-flows.md](../architecture/data-flows.md)

## Context

A partition is the unit of parallelism and ordering. Keys must preserve necessary source, zone and incident ordering; distribute uneven load, with one site producing 30% in `LOAD-50K`; keep correlation state local; and prevent incident-creation races. Increasing an existing topic's partition count changes key placement.

## Considered options

### Normalized-event key

| Option | Advantages | Disadvantages |
|---|---|---|
| `site_id` | Site-level rules remain local | A high-load site overloads one partition |
| **`site_id:zone_id`** | Site load is spread across zones; zone-, device- and source-level rules remain local | Rules with a broader scope require repartitioning |
| `source_id` | Ideal distribution | Almost every correlation rule requires repartitioning |

### Signal key

| Option | Advantages | Disadvantages |
|---|---|---|
| `correlation_key` | Matches the rule-state key | Signals from different rules with the same grouping key can enter different partitions. Two Incident Service instances could create an incident for one `grouping_key` simultaneously, requiring a uniqueness error and retry to resolve the race |
| **`grouping_key`** | Every signal for the future incident is ordered within one partition, preventing the race | - |

## Decision

1. Use keys from the [data-flows.md summary](../architecture/data-flows.md#4-summary-table): in particular, `psim.ingest.raw.v1` uses `source_id`, `psim.events.normalized.v1` uses `site_id:zone_id`, `psim.signals.v1` uses `grouping_key`, and the catalog uses `resource_type:resource_id`.
2. Rules whose correlation key is derived from a zone, including zone, device and source rules, execute locally. Rules with a broader key publish filtered events to internal topic `psim.correlation.repartition.v1`, keyed by `correlation_key`, in the same transaction. Rule validation rejects such rules without an event-type filter.
3. The installer sets partition counts from the profile. Defaults are in [scaling.md](../architecture/scaling.md#2-number-of-partitions), including 48 partitions for cluster event streams. Change counts only through a new topic version.
4. Composite keys use `:` as the separator and canonical UUID components.
5. Detect high-load keys through partition-skew metrics and alerts, apply per-connector limits, and recommend splitting an overloaded zone.

## Consequences

- `psim.correlation.changelog.v1` and `psim.normalizer.state.v1` have the same partition counts as their input topics: co-partitioning.
- Moving a device to another zone changes its event partition. Ordering across the move is not guaranteed; correlation uses event time under ADR-010.
- Growth beyond 10x requires a `v2` topic and migration; see [scaling.md](../architecture/scaling.md#6-scaling-beyond-mvp).

## Verification

Run an uneven-profile load test with `LOAD-50K`, display partition-skew metrics on the dashboard, and test the rule validator.
