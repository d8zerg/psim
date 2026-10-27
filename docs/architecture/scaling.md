# Scaling and Partitioning

<!-- language-links -->
[English](scaling.md) | [Русский](../ru/architecture/scaling.md)
<!-- /language-links -->

All numbers in this document are project estimates. They require qualification measurements and load testing before they can support a validated capacity model.

---

## 1. Principles

1. **The unit of parallelism is a partition.** Each stage of the pipeline is scaled by adding instances to the consumer group up to the number of partitions in the input stream.
2. **The number of partitions is set with a margin during installation.** Increasing the number of partitions in an existing stream changes key distribution and disrupts order, so this is only done through a new version of the stream with migration (see section 6).
3. **Services without state or with state tied to a partition.** The correlation state and Normalizer tracker move with the partition; other states are stored in PostgreSQL and Redis.
4. **Hot paths do not depend on synchronous calls.** Reference data is local copies from compacted streams.

## 2. Number of Partitions

| Stream | Cluster | One Machine | Reason |
|---|---|---|---|
| `psim.ingest.raw.v1` | 48 | 12 | 50,000 events/s ≈ 1,000/second per partition; buffer up to ×20 based on number of consumers; 48 divided by 2, 3, 4, 6, 8, 12, 16, 24 |
| `psim.events.normalized.v1` | 48 | 12 | Same as raw; `psim.correlation.changelog.v1` and `psim.normalizer.state.v1` replicate the number of partitions of their input |
| `psim.correlation.repartition.v1` | 12 | 4 | Only filtered events from rules broader than a zone |
| `psim.signals.v1` | 12 | 4 | Signal flow is dozens of times less than event flow |
| `psim.incidents.events.v1`, `psim.response.events.v1` | 12 | 4 | - |
| `psim.commands.v1`, `psim.commands.results.v1` | 6 | 2 | One command per second |
| `psim.catalog.v1` | 6 | 2 | Read entirely |
| `psim.config.v1` | 1 | 1 | Single order of configuration changes |
| `psim.audit.v1` | 6 | 2 | Key is tenant; in MVP, it’s actually one active partition |
| `psim.dlq.*.v1` | 3 | 1 | - |

Numbers are default values during installation; they are set during installation based on load profile.

## 3. Correlation and Keys

The input key `psim.events.normalized.v1` is `site_id:zone_id`. Rule state is stored in `correlation_key`. There are two cases:

| Key Correlation | Example | Handling |
|---|---|---|
| Derived from a zone: zone, device, source | “perimeter sensor and video analytics in the same zone within 30 seconds” | Locally in the input partition, without redistribution |
| Broader than a zone: site, building, floor, tenant | “five alarms in different zones of one site within 10 minutes” | An event that passed the rule filter is published in `psim.correlation.repartition.v1` according to `correlation_key` in the same transaction; the rule state lives with the partition owner after redistribution |

The rule filter applies before redistribution, so only a small portion of the flow passes through the redistribution stream even for “hot” sites. Redistribution adds one transaction to the signal delay for such rules; budget - see [quality.md](quality.md#4-delay-budget).

## 4. Hot Keys

| Situation | Risk | Mitigation |
|---|---|---|
| One site provides 30% load (profile `LOAD-50K`) | Overloading some partitions | The key includes the zone: site load is distributed across its zones |
| One zone provides a flow close to the partition’s throughput capacity (mass activation) | Increase in partition delay | Metric for partition and alert; administrator divides the zone; limits on connectors in the gateway |
| Site-level rules on unfiltered flow | Overloading of redistribution partition | Rule validators prevent rules broader than a zone without filtering by event type (validity of rule language) |
| One incident receives thousands of signals | Growth of the timeline and incident events | Suppression in rules (`suppression`); aggregation of similar signals in the timeline |

## 5. Capacity planning

### 5.1 Initial Assumptions

| Parameter | Value |
|---|---|
| Size of raw event (Protobuf) | 400 B |
| Size of normalized event (Protobuf) | 600 B |
| zstd compression factor for identical events | ×4 |
| Percentage of significant events (`severity ≥ medium`) | 5% |
| Size of event row in ClickHouse after compression | 50 B (requires load measurement) |
| Signals per 1,000 events | 1 |
| Active incidents | up to 10,000 |

### 5.2 Estimation at 50,000 events/s

| Resource | Calculation | Total |
|---|---|---|
| Kafka write, raw | 50,000 × 400 B / 4 | 5 MB/s |
| Kafka write, normalized | 50,000 × 600 B / 4 | 7.5 MB/s |
| Kafka disk on replica, raw, 6 hours | 5 MB/s × 21,600 s | 108 GB |
| Kafka disk on replica, normalized, 24 hours | 7.5 MB/s × 86,400 s | 650 GB |
| Kafka disk on cluster (RF = 3), with 30% buffer | (108 + 650 + etc ≈ 50) GB × 3 × 1.3 | ≈ 3.2 TB |
| Insertions into ClickHouse, table `events` | All events in batches ≥ 20,000 rows | 50,000 rows/s, 2-3 insertions/s |
| ClickHouse disk, all events, 7 days | 50,000 × 50 B × 604,800 s | ≈ 1.5 TB |
| ClickHouse disk, significant events, 90 days | 2,500 × 50 B × 7,776,000 s | ≈ 1 TB |
| ClickHouse disk on replica with 30% buffer | (1.5 + 1 + links and facts ≈ 0.1) TB × 1.3 | ≈ 3.4 TB |
| Signals | 50,000 / 1,000 | 50/s |

The history of events in ClickHouse ([ADR-028](../adr/0028-clickhouse-analytical-storage.md)) is the largest storage cost. Parameters that affect it include retention for all and significant events, threshold for significance, percentage of significant events for specific clients, actual compression level. The capacity model based on load measurements provides estimates based on these parameters.

The default retention for normalized events is 24 hours. It limits the depth of rule testing on history; increase is done during installation with recalculation of disks.

### 5.3 Service Instances at 50,000 Events/s, Cluster

| Service | Instances | Estimated Load per Instance |
|---|---|---|
| Connector Gateway | 2 (+1 reserve) | 25,000 events/s |
| Normalizer | 3 | ≈ 17,000 events/s |
| Correlation Engine | 3 | ≈ 17,000 events/s |
| Event History | 2 | 25,000 events/s, insertion in ClickHouse in batches |
| Incident Service | 2 | 25 signals/s |
| Response Engine, Command Service, Notification Service | 2 each | Low |
| Projection Service | 2 | ≈ 5,000 events/s per state change |
| API Gateway | 2 | 250 consoles |
| Audit Service | 1 active + 1 reserve | Hundreds of records/s |
| Resource Catalog | 2 | Low |

### 5.4 Standard Test Environments

| Test Environment | Composition | Target Load |
|---|---|---|
| One machine | 16 vCPU, 64 GB RAM, NVMe 4 TB | 10,000 events/s (preliminary estimate, requires load confirmation) |
| Cluster on VMs | 3 infrastructure nodes (Kafka, PostgreSQL, Redis, ClickHouse with Keeper; ClickHouse - on two of three): 16 vCPU, 64 GB, NVMe 8 TB; 2 application nodes: 16 vCPU, 32 GB | 50,000 events/s sustained, 75,000 events/s at peak |

## 6. Scaling Beyond MVP

Path to millions of events/s without changing the architecture:

| Step | What changes |
|---|---|
| ×2-×10 | More brokers and service instances within the 48 partitions established |
| ×10-×50 | New version of topics with a larger number of partitions (`v2`): double writing, switching consumers by time boundary, retirement of `v1` |
| ×50 and more | Sharding across sites or regions: multiple Kafka clusters and pipelines with an incident federation (outside MVP, X4, X10); tiered storage in Kafka for long retention |

Limitations that need to be removed before scaling ×10: sequential audit chain on tenants (transition to a chain on a partition with a shared root at a checkpoint), ClickHouse (transition from 1 shard to multiple shards across sites, ADR-028).
