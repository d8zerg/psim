# ADR-009. Correlation state storage: RocksDB with a changelog topic

<!-- language-links -->
[English](0009-correlation-state-store.md) | [Русский](../ru/adr/0009-correlation-state-store.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-006, ADR-007, ADR-010, ADR-011

## Context

Correlation rules for windows, sequences, thresholds and absence accumulate state by correlation key. State must change atomically with output signals and offsets for exactly-once semantics, move with partitions during rebalancing, recover after a failure within 30 seconds RTO, and exceed RAM capacity for long windows. No ready-made Kafka Streams equivalent exists for C++.

## Considered options

| Option | Advantages | Disadvantages |
|---|---|---|
| Kafka Streams or Flink on the JVM | Existing state and EOS model | Another language and runtime in the core, conflicting with the C++ server constraint; Flink-cluster operation |
| In-memory state with periodic snapshots | Maximum speed | State between snapshot and failure is lost or requires long input replay; limited by RAM |
| PostgreSQL state | Familiar storage | No shared transaction with Kafka; requires inbox/outbox or another coordination protocol. Batching is possible but needs separate performance measurements |
| Redis state | Fast | No Kafka atomicity; Redis is not a source of truth under ADR-015 |
| **Local RocksDB and a changelog topic in the same Kafka transaction** | Atomicity with output and offsets; state can exceed RAM; changelog recovery on any node; established Kafka Streams model | Custom recovery and compaction; recovery time grows with state size |

PostgreSQL remains a valid option with a different commit model. RocksDB is chosen for atomicity of signals, changelog and input offsets in Kafka; the 50,000 events/s target alone does not establish PostgreSQL limitations. RocksDB holds a recoverable local copy, while Kafka determines committed state.

## Decision

1. Every assigned input partition has its own RocksDB instance in the service's local directory, with a column family per rule and tenant.
2. Record every state change in `psim.correlation.changelog.v1`, compacted and co-partitioned with `psim.correlation.repartition.v1`, keyed by `tenant_id/rule_id/rule_version/correlation_key`, in the same transaction as signals and offsets. State deletion uses a tombstone.
3. Write a RocksDB batch only after the Kafka transaction commits successfully. Discard the RocksDB batch if the transaction aborts. A local file records the changelog offset through which local state is consistent.
4. On partition assignment, if local RocksDB is consistent through offset X, replay the changelog from X. Otherwise, recreate the entire database from the changelog.
5. Bound state through the maximum rule window, R3, which defaults to 24 hours. Window state stores match masks, identifiers and projections of fields required by rule expressions. Unused attributes are omitted; TTL follows event time.
6. For RocksDB 10.10.1, select Apache-2.0 from the two upstream alternatives. SBOM records the selected license and the original alternative. The general policy does not permit GPL for statically linked dependencies.
7. Standby state replicas that continuously read the changelog are outside the MVP. Enable them if Correlation Engine recovery measurements exceed 30 seconds.

## Consequences

- Local storage is isolated behind the `StateStore` adapter. The evaluator receives state as a value; simulation stores that value in memory.
- The node needs fast local storage, such as NVMe. The state directory does not require backup because it is recoverable from the changelog.
- Recovery time is a key metric, with an alert above 30 seconds.

## Verification

Simulation tests inject failure and rebalance at any point and compare signals against a reference. Measure recovery with the reference state volume.
