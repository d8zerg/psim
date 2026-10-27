# Live Stateful Correlation

<!-- language-links -->
[English](live-correlation.md) | [Русский](../ru/engineering/live-correlation.md)
<!-- /language-links -->

The Correlation Engine applies published sets of rules to the normalized stream. Decisions regarding local state and time are defined in [ADR-009](../adr/0009-correlation-state-store.md) and [ADR-010](../adr/0010-time-semantics.md). Version management and historical processing are described in the [rule guide](correlation-rules.md).

## Processes and Partitions

The role `live` initiates routing and computation in a single process. The router uses shard 0. Each shard has its own computing unit; shard 0 also handles routing. Role `engine` uses all shards. Each computing unit has its own Kafka clients and assigned partitions. The testing environment uses three processes with four shards each and eight blocking operations threads. Twelve computing units receive one input partition each. Roles `router` and `engine` allow for separate allocation; `management` provides API management. All instances of the same role use a shared consumer group. The readiness of the working process requires assigning input partitions and completing recovery.

Event path: `psim.events.normalized.v1` -> `psim.correlation.repartition.v1` -> `psim.signals.v1`. The router selects the active set of rules and records events with unchanged rule versions. Repartitioning applies to all scopes, including the site and building: events from different zones are sent to a single correlation key owner. The rule set is completely changed between transactions. Already recorded internal messages retain their original version.

The repartition and changelog topics have the same number of partitions: 12 in the local topology, 48 in the cluster. Signals are divided by the key of the future incident group. The absence of a published set does not include an embedded rule; the demonstration set is published by the testing tool.

## Recording and Recovery

Kafka is the source of confirmed state for correlation. One transaction records output signals, changelog entries, suppression explanations, and input offsets. Uncompleted changes remain separate from the confirmed local state and are discarded when a transaction is canceled.

After Kafka confirms the commit, batches are stored in RocksDB through a limited pool of blocking operations. Each assigned partition has its own database; the column family separates rules and tenants. The state key contains tenant, rule, its version, and correlation key. Along with the packet, the next offset from the changelog is atomically saved.

A crash before Kafka commit replays the input; a crash after commit preserves the output and recovers lagging RocksDB state from the changelog. When assigning a partition, `read_committed` reaches the set upper limit of the log. The local copy is used for incremental catch-up; absence of group offsets, out-of-log checkpointing, or outdated checkpoints cause a complete rebuild. Reading and rebuilding are performed in bounded batches.

The local copy is considered eligible for incremental catch-up for no longer than an hour. Changelog tombstones are retained by the broker for one day. This prevents using an old copy that might have missed the already deleted tombstone. The local database does not require backup.

Timer transactions also include group offsets to prevent the previous partition owner from publishing after reassignment. When partitions are revoked, unfinished transactions are canceled. The live processing commit interval is 10 ms; the pause between transactions is 5 ms: it reduces repeated calls to the coordinator until the markers of the previous transaction are completed. The transaction timeout is limited to 10 s; the local broker uses a session timeout of 10 s.

## Time, State, and Maintenance

Processing uses event time. The watermark does not decrease; maximums from active partition sources within the tenant and allowed lateness of the rule are considered. The default allowed lateness is 10 s, and the idle timeout is 30 s. Idleness is determined by the processing time of the observation, not by the old receipt time at the gateway. After removing idle sources, timers continue working without new events.

`single`, `conjunction`, and `sequence` are calculated when a final event arrives. The immediate match is determined by Kafka processing order; a late suitable candidate does not change the already sent signal. Repeating from the same log retains the composition and ID. Historical processing with time sorting can select other evidence in case of ambiguous matches. `threshold` and `absence` sort delayed events by time and identifier and calculate the confirmed part of the stream by the watermark. Half-open windows: an event at exactly the boundary does not cancel absence. Closed windows are not reopened; delayed events are counted by `psim_correlation_late_events_total`. The full history remains with Event History. If there is a non-zero interval for repeated suppression, a new signal is allowed starting from `last_signal_at + suppression`; earlier times remain suppressed. With zero intervals, this restriction does not apply.

The future time of the source is limited by the acceptable discrepancy with the receipt time. Events older than the acceptable age do not create live signals. `live_observation`, session identifier, and observation timestamps retain the value set by the normalizer. A historical event itself does not confirm the current normal state of the device; the condition for current observation uses `event.live_observation`.

Maintenance is selected based on historical policy and event receipt time; the timer uses the waiting limit. Suppression does not start the interval for repeated suppression. Fire events and P1 pass. The audit transaction record `correlation.maintenance_suppressed` contains a signal and exact policy versions. The working engine does not change open incidents and their SLA.

## Constraints and Placement

The open state stores identifiers, matching masks, expectation boundaries, and field projections required for rules expressions. Unused attributes are excluded. The state limit for a key is 1 MiB, and there are restrictions on the number of pending elements. The source counter uses a window of 4096 sequences. The number of keys is limited to `settings.max_live_keys` (default: 100,000 per worker). Each worker’s hot cache is limited to 4096 keys and 64 MiB. The timer index only stores keys and timestamps; the full state is loaded from RocksDB as needed. Block caches and memtable budgets are shared between process databases.

`settings.state_directory` defaults to `/var/lib/psim/correlation`. The process requires a separate, writable local volume. Multiple instances do not share a common directory. Each worker’s directory is separated by a shard number so it can be restored independently from its previous owner during reconfiguration. The development environment uses `/tmp/psim-correlation` within a separate container; container restarting checks cold recovery.

## Tests

```bash
task correlation:test PROJECT=psim-correlation-test
task correlation:sanitizers PROJECT=psim-correlation-test
task correlation:bench PROJECT=psim-correlation-test
task bench:rules
task rules:coverage PROJECT=psim-rules-test
task correlation:sim
```

Component testing runs three instances, compares signals during repeated delivery, correlates different zones, SIGKILL, and partition reconfiguration, and checks timers without new events. Reports are in `build/correlation/<preset>/`. Load testing uses one active rule `single` with typed conditions and template, 1024 keys, three processes with four shards each. Measurement starts after the state is `Stable` for both consumer groups and 512,000 warmed-up events separately. Then, 1,500,000 events are sent at a rate of 51,000 per second. Warmup and measurement use the same keys with ongoing source sequences. Identifiers of all events are compared; latency is measured until the consumer receives an acknowledged signal using `read_committed` with `fetch.wait.max.ms=1`. The report includes p99 for the entire measurement and breakdown by seconds. The threshold is 50,000 events per second, and overall p99 should not exceed 100 ms. After SIGKILL, all three processes are checked for cold recovery with 1024 keys and no additional signals when original messages are repeated. RocksDB environment is stored in tmpfs; this profile does not measure disk latency. This is a short local measurement; longer SLO tests are part of full load qualification.

B-04 measures all five types of rules with typed expressions separately from transport. Manual simulation uses Kafka models and actual compute units: 200 seeds by default, long run - 10,000; crashes before commit, after commit, before local copy update, owner change, and independent verification of expected signals. It is not included in CI. Tests of actual adapters and working processes are part of the CI environment phase.
