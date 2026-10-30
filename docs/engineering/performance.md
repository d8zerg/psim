# Performance qualification and sizing

<!-- language-links -->
[English](performance.md) | [Русский](../ru/engineering/performance.md)
<!-- /language-links -->

The mandatory profiles and limits come from the [acceptance specification](../product/acceptance-spec.md), [verification matrix](../quality/verification-matrix.md) and [latency measurement rules](../quality/latency-measurement.md). Successful component benchmarks do not qualify the complete pipeline.

## Reproduction

Use native Docker Engine and Docker Compose on Linux. Build the current working tree before creating the owned VM cluster:

```sh
task deploy:images
task deploy:vm:up
task load:nightly
task load:qualify
```

`load:nightly` exercises the instruments at 5,000 events/s for 60 seconds. It does not qualify LOAD-50K. `load:steady` and `load:soak` each measure 50,000 events/s for 600 seconds. `load:peak` measures 75,000 events/s for 300 seconds. Every run has a separate 30-second warmup, drained before measurement. `load:consoles` runs the steady profile, including 500 WebSocket connections and a real operator browser. The cluster remains available after the run; the load processes are removed.

The fixture uses 100 independent Connector SDK clients by default, each with its own connector identity, gRPC connection and durable journal. It distributes 10,000 devices across those clients and 10 sites without sharing sources. LOAD-50K averages 500 events/s per client before the scheduling margin. Set the client count with `python3 -I tools/load/check.py steady --clients 100`; supported counts divide 1,000 and range from 2 to 100. An existing owned catalog is reassigned through public APIs without creating additional devices. Subsequent profiles reuse this catalog only after checking its connector and installation identity; each phase advances the source epoch. One site receives 30% of background events. One alarm per second exercises correlation, incidents, projections and browser rendering. Background events are normalized and persisted. Warmup and measurement use different source epochs. This fixture measures the single-event correlation rule; other rule semantics remain covered by component and simulation suites.

The generator is C++ and uses Connector SDK. Python only prepares the environment, runs instruments and evaluates evidence. The offered rate includes a 2% scheduling margin for the full profiles. The gate uses acknowledgements before draining, not the offered rate or the final acknowledged count. Raw and normalized Kafka identifiers are reconciled. ClickHouse is checked independently against per-source sequence ranges, counts and uniqueness. Event storage is measured from active `system.parts` on one replica, without multiplying replica copies into the per-event estimate.

Public console, history and report reads run concurrently. Only requests wholly inside the measured interval contribute to query latency. History uses a one-day filter; reports use a 30-day filter. The result also depends on populated cardinality, so these filters alone do not prove performance on a fully populated retention period. Browser timestamps are joined to source event times through incident correlation identifiers; Kafka observation alone is insufficient evidence of rendering. All VM and browser clocks must satisfy the acceptance clock bound.

Soak evidence includes application resident memory and consumer lag every 10 seconds. The last third is compared with the first third: RSS may not grow by more than 10% plus 64 MiB per process, and average total lag may not grow by more than one second of target traffic. Missing processes or measurements fail qualification. These explicit tolerances accommodate allocator retention and batch boundaries; they do not prove an absence of leaks over longer periods.

Results and command logs are written to `build/load/<profile>/`. A failed or interrupted run writes `passed: false`; it cannot reuse a previous successful report. Generated deployment credentials remain in the private installation directory. No upgrade, rollback, chaos injection or optional two-configuration comparison is performed by these commands.

For diagnostics, the browser observer accepts `PSIM_LOAD_CONSOLES` from 1 to 500; its default remains 500, as required for qualification. The Kafka observer retains individual signal and incident timestamps in `observations`, including delays beyond the histogram's 100-second limit. The browser retains primary connection frames in `incident_deliveries` and reports progress in `load-browser-progress.json`. Frame `observed_ms` is an asynchronous automation timestamp that can follow the DOM update; it is not the browser's socket receipt time. End-to-end rendering uses `renders` joined to source event times. Report expected, observed and rendered counts with percentiles: latency among a partial set does not describe events still queued or missing. One alarm per second under background load measures incident latency, not maximum incident throughput.

## Regression gate

`task ci` retains B-05: release benchmarks are compared with the master baseline and a slowdown greater than 5% fails. Changes to load tooling select this gate and `task load:test`, including negative tests for shortened intervals, missing observations, errors and acknowledgements completed only during drain. Full cluster qualification is explicitly run with `task load:qualify`; it requires the prepared VM installation and is not silently substituted with the short CI profile.

## Capacity model

Run `task load:sizing INPUT=<input.json> OUTPUT=<output.json>`. Inputs are explicit; values from a short run must not be presented as validated capacity. The output sets `validated_scale_out: false` because only one configuration is required for qualification.

[Example inputs](../../tools/load/sizing.example.json) list every field with illustrative assumptions. Replace them with measurements and retention policies before using the estimate for procurement.

| Resource | Calculation before utilization reserve |
|---|---|
| ClickHouse | events/s × 86,400 × retention days × measured disk bytes/event × replicas |
| Kafka | events/s × 86,400 × retention days × aggregate bytes/event across retained streams × replicas |
| SeaweedFS | sites × floors/site × bytes/plan × retained versions × physical copies |
| PostgreSQL | measured or budgeted logical database size × replicas |
| External backup | (one logical history copy + plans + PostgreSQL) × retained full copies + daily changed bytes × incremental days |
| Application instances | ceil(target rate / measured rate × measured instances / maximum utilization) |
| Catalog writes per tenant | batch rows × 1,000 / measured transaction duration in milliseconds |

Provisioned capacity divides each storage estimate by maximum utilization, normally 0.7. `event_compressed_bytes`, `event_uncompressed_bytes` and `event_disk_bytes` come from the history report; disk bytes include indexes and marks. Kafka bytes/event must include all retained streams, not just raw input. Backup assumptions must include WAL, identity/configuration material and change volume; independent backups must reside outside the production failure domain. Temporary backup staging, compaction space and restore throughput require separate operational budgets.

SeaweedFS stores floor plans and their retained versions, not camera video. The backup model counts logical data once per backup, without multiplying production replicas. Replication and erasure coding are alternative layouts; `seaweed_copies` must reflect the selected physical layout.

The catalog serializes mutations within one tenant using a transaction advisory lock ([ADR-051](../adr/0051-resource-catalog.md)). Additional instances improve availability and reads, but cannot multiply that tenant's write throughput. Import batch size and transaction duration determine its upper bound. Independent tenants can proceed concurrently until the shared PostgreSQL limit is reached. The application estimate is linear extrapolation, bounded by partitions, hot keys, shared stores, CPU and network; it is not an automatic deployment recommendation.

A single-client run measures the credit window and ACK cycle of one connection; it does not establish aggregate platform capacity. Multi-client qualification requires unique identities, consistent counters and the target pre-drain throughput for every client. The mean queue of the last ten samples may exceed the first ten by no more than two negotiated 1,000-event batches. Missing clients, unknown source owners and compensation for a slow client by a fast one fail qualification. All clients connect before the common measurement interval. Kafka identifiers are reconciled once across all clients; ClickHouse ranges retain the connector identity of each source.

## Diagnosing slow acknowledgements

The generator creates independent SDK clients in one process on one host. This exercises concurrent authenticated connections and separate durable queues, while distributed generators and different client network conditions remain separate scenarios. Publication batches are staggered across clients. Kafka reconciliation starts at the partition offsets captured before generation; it does not rescan earlier runs.

The gateway prepares large batches in bounded chunks, allowing registry refresh and other sessions to run between chunks. Trust snapshots use a separate PostgreSQL pool and refresh independently of durable gateway presence and expired-session cleanup. Delayed control writes therefore do not postpone the next trust read. The one-second freshness limit and cross-instance revocation barrier remain enforced. Network regression tests block presence writes and trust reads separately: the former must preserve a valid session, while the latter must close it.

An SDK local buffer overflow means acknowledgements cannot keep up with publication. Compare sent batches and ACK counts, queue depth, ACK wait time, and local buffer operation time to identify the cause. The `psim_connector_acknowledgement_max_seconds`, `psim_connector_buffer_acknowledgement_max_seconds`, and `psim_connector_pending_read_max_seconds` metrics are client lifetime maxima, not measurement-window percentiles. The gateway histogram `psim_gateway_batch_stage_seconds` separates the `quota`, `prepare`, `kafka`, and `acknowledge` stages.

Shared deployment resources affect ACKs: frequent empty PostgreSQL and Kafka requests can delay Kafka writes even when the SDK connection is healthy. The shared runtime uses the platform library's Kafka batching and fetch-wait defaults. Outbox checks an empty queue with an indexed query without opening a publication transaction; it polls after 50 ms on an empty result, 1 ms after publication, and 250 ms after an error. Publication retains the advisory lock, row locks, and delivery marking after Kafka acknowledgement. An empty catalog import queue is also polled after 50 ms.

After Kafka confirms delivery, the gateway sends `Credits` before `BatchAck`. This restores the window before the ACK allows the SDK to send its next batch. The reverse order could split a full window into alternating partial batches even with queued events. Returned credits do not acknowledge delivery by themselves: the SDK removes events from its local buffer only after the corresponding `BatchAck`.

The console projection saves an event batch in one PostgreSQL transaction. SQL commands use pipelines bounded to 128 commands and approximately 1 MiB; a single large command remains subject to the PostgreSQL client's request limit. Every resource change and history entry is preserved, including consecutive updates to the same source within a batch. An error rolls back the whole batch together with its Kafka positions. Informational events do not create separate transactions, and unchanged positions are not written again. This reduces disk synchronizations without losing intermediate history.

Compare performance without concurrent builds or static analysis on the generator host. Draining the queue after generation stops confirms delivery but does not by itself establish sustained ACK throughput.

The processor waits for input when no transaction or uncommitted offsets are present; an expired idle commit deadline must not force a one-millisecond poll loop. Kafka queue notifications still wake the consumer on arrival. To locate transaction overhead, compare the deltas of `psim_kafka_transaction_stage_seconds_sum` and `psim_kafka_transaction_stage_seconds_count` by service and `stage`: delivery wait (`flush`), blocking-pool queue (`pool_wait`), offset transfer (`offsets`) and transaction completion (`commit`). Compare only matching measurement windows and report backlog separately from throughput.

Normalizer uses enqueue-only EOS-K transactions. Native `rd_kafka_flush` forces accumulated normalized events and coalesced source cursors into delivery without waiting for `linger.ms`; offset registration follows successful delivery in the bounded blocking pool, before `EndTxn`. Serial registration avoids competing coordinator updates from `AddPartitionsToTxn` and `AddOffsetsToTxn`. The producer pump stays paused while native callbacks run in the pool, and tracked `send`/`send_batch` calls are rejected in this mode. Other producers keep their existing behavior. Delivery, cursor state and input positions remain atomic.

Normalizer fetches at most 512 records and commits after at most 512 handled inputs or 8 MiB of enqueued key, value and header bytes. The byte limit is checked after each handler; one record and the coalesced cursor records prepared by `before_commit` can exceed it. The commit interval is 5 ms between polls and 20 ms while handling fetched records. There is no wait to fill a batch. The consumer queue is limited to 16 MiB per worker, with a 16 MiB fetch limit; concurrent broker responses and a single larger Kafka record can exceed the queue threshold.

`settings.workers_per_shard` defaults to 2, with 1-8 workers per executor and at most 64 workers per instance. Each has independent transaction and consumer ownership; worker zero preserves the previous transactional ID. Runtime executor threads stay unchanged. Native Kafka handles still create their own background threads, so worker count remains a sizing parameter. Size `runtime.blocking_threads` to at least the active worker count; additional capacity can serve startup and control operations. Generated deployments allocate `max(4, 4 * runtime.shards)` threads for the default configuration. Catalogs, mappings and cursor state are already held in memory; an external cache would add network operations to this path.

Use `psim_normalizer_commit_seconds` for the complete wait, including cursor preparation. Diagnostic images that overlap `flush` and `offsets` must not add their phase means as elapsed transaction time. `pool_wait` measures each queued native call separately.

A diagnostic CPU comparison kept the service images and four executor threads/eight Kafka workers per Normalizer instance unchanged. With 100 SDK clients, one console and a 30-second target of 50000 events/s, increasing VM CPU allocation from 4/4/4 to 6/6/4 raised ACK from 41702.2 to 44753.3 events/s and sampled normalization from 12681.4 to 15378.2 events/s. All 1530000 events were reconciled in raw and normalized with no duplicates; all 30 incidents reached the DOM. Event-to-DOM p50/p99 were 32.389/49.828 seconds. The SDK queue still contained 187072 events before drain, so sustained target throughput is not established. A 20-second warmup at 5000 events/s preceded the new run; the control used a different warmup sequence. This is a single diagnostic comparison, with all VMs and SDK journals on one physical SSD. Evidence: `build/load/cpu664-50k-30s/comparison.json` and `summary.json`.

## Qualification report

Full-load qualification is not yet established. Machine-readable results are the evidence of each attempted profile. The mandatory step remains open until sustained and peak rates, loss/duplicate checks, concurrent reads, browser latency, soak stability and measured row sizing all pass. Upgrade qualification S12/C-12 and validation on a second hardware configuration are outside this scope.
