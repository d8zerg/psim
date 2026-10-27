# ADR-041. Kafka layer

<!-- language-links -->
[English](0041-kafka-layer.md) | [Русский](../ru/adr/0041-kafka-layer.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-002 (Kafka and librdkafka), ADR-003 (Schema Registry), ADR-004 (wire format), ADR-005 (trace headers), ADR-006 (delivery semantics; refines EOS-K), ADR-016 and [ADR-040](0040-async-primitives.md) (execution model), [crosscutting.md](../architecture/crosscutting.md#33-kafka-transactions-eos-k) sections 3.3 and 4, methods TS-03, B-01, FF-04

## Context

The Kafka client library needs librdkafka integration with Asio without blocking polls, idempotent producers and transactions, manual consumer offset commits and correct rebalancing, batching, backpressure, retries and DLQ, Schema Registry Protobuf encoding with schema caching, trace headers, and lag and throughput metrics. Acceptance requires integration tests demonstrating no loss and exactly-once effects during forced crashes, and at least 200 000 messages/s for both producer and consumer.

ADR-006 and crosscutting.md 3.3 originally prescribed one transactional producer per assigned input partition (`transactional.id = <service>-<topic>-<partition>`). This predates KIP-447: 12-48 partitions per instance require dozens of librdkafka clients, each with threads and connections.

## Options considered

### Integrating librdkafka with Asio

| Option | Advantages | Disadvantages |
|---|---|---|
| Polling thread per client (`rd_kafka_poll` loop) | Simple | Extra threads and context switches; callbacks on another thread need shard synchronization |
| librdkafka background queue (`rd_kafka_queue_get_background`) | No polling | Callbacks run on a librdkafka thread and need the same synchronization |
| **`rd_kafka_queue_io_event_enable` + eventfd in Asio** | Callbacks on the owning shard, without extra threads or locks (ADR-016, item 5) | Notification only on empty -> nonempty transition; drain the queue before waiting and add a 100 ms fallback wakeup |

### Exactly-once (EOS-K)

| Option | Advantages | Disadvantages |
|---|---|---|
| Producer per input partition (original ADR-006) | Partition isolation | Dozens of clients per instance; producers created during each rebalance |
| **Producer per consumer (KIP-447): `send_offsets_to_transaction` with group metadata** | One transactional producer per shard; group generation fences stale consumers and `transactional.id` fences previous process runs | A transaction spans every shard partition; abort rewinds them all |

### Rebalancing

Use **KIP-848** (`group.protocol=consumer`, Kafka 4.x), with classic cooperative rebalancing configurable. Reject static membership (`group.instance.id`): KIP-848 rejects a crashed member restarting with the same id before its session expires (`UNRELEASED_INSTANCE_ID`), which librdkafka treats as fatal. Failed members' partitions move after `group.consumer.session.timeout.ms` (45 seconds by default, 10 seconds locally).

## Decision

The infrastructure library `libs/platform/kafka` (`psim::platform_kafka`) binds clients to a shard executor, like ADR-040 primitives.

1. **Producer** is always idempotent: `enable.idempotence=true` and `acks=all` are fixed; overrides are configuration errors. Defaults are zstd and `linger.ms=5`. `enqueue` waits only when the local queue is full, applying backpressure through `WaitList`; `flush` reports delivery results. `send` waits for acknowledgement (`acks=all` before replying to a connector, ADR-006 item 2). A queue-event coroutine handles delivery reports.
2. **Transactions:** `transactional_id = <service>-<instance>-<shard>`. Blocking librdkafka `init/commit/abort` calls run in `BlockingPool` (ADR-016 item 4). Deliver messages asynchronously before committing, so commit does not wait for sends. Abort after a failed commit. On `fatal()` (fencing by a newer instance or a fatal idempotence error), recreate the producer and fail the stage.
3. **Consumer** uses manual offsets only (`enable.auto.commit=false`, `enable.auto.offset.store=false`) and `read_committed` for EOS-K inputs. `poll` returns a batch and discards messages from partitions revoked during that poll. `RebalanceListener` runs inside `poll`, between batches; completed work is committed synchronously on revocation. `close` leaves the group asynchronously within 30 seconds; without it, membership expires by session timeout.
4. **Processor** runs each shard's read-process-write loop. Stage logic implements `Handler::handle(const Message*, Output*)`, using pointers instead of coroutine reference parameters (ADR-040):
   - ALO: deliver outputs (`flush`), then commit offsets every `commit_interval` (default 1 second); delivery failure rewinds partitions to committed offsets;
   - EOS-K: outputs, DLQ and offsets share one transaction (20-50 ms interval, ADR-006 item 5); failure aborts and rewinds partitions to the transaction start;
   - handler errors: a catalog `retryable` error rewinds and pauses that partition, then resumes it with exponential backoff while other partitions continue (ADR-006 item 4). Non-retryable errors go to `psim.dlq.<stage>.v1` with original bytes and `psim-dlq-*` headers (data-flows.md 3.10). Exceptions are programming errors: route to DLQ and stop the partition after `circuit_breaker` consecutive failures (crosscutting.md 4).
5. **Serialization:** `ProtobufSerde<T>` implements Confluent wire format: magic byte, schema id, zigzag-varint message indexes and body. Resolve the writer schema id from the latest `<topic>-value` subject at startup. `SchemaRegistry` is a small Beast client with an id cache. Reads accept any id; FF-04 ensures compatibility and the registry stays off the hot path (ADR-003). Invalid encoding or an unrelated message type yields `PROCESSING_PAYLOAD_INVALID` and DLQ.
6. **Schema registration:** `tools/contracts/registry.py` serves the installer and CI (ADR-003), registering each topic value from `contracts/topics/topics.yaml` with imports as references. Imported files use import-path subjects with `FULL_TRANSITIVE`; topic subjects use `BACKWARD_TRANSITIVE` (ADR-004). The registry includes `google/protobuf/*`. Run `task contracts:registry:register`.
7. **Tracing:** `inject`/`extract` W3C `traceparent`/`tracestate` headers. Custom `traceparent` parsing and formatting avoid an OpenTelemetry propagator false positive from `security.ArrayBound` without disabling that security check. `start_consumer_span` creates a CONSUMER span parented by the message context.
8. **Metrics** use one `KafkaMetrics` per process: `psim_kafka_produced_messages_total`, `psim_kafka_produced_bytes_total`, `psim_kafka_delivery_errors_total`, `psim_kafka_consumed_messages_total`, `psim_kafka_consumed_bytes_total` by topic; `psim_kafka_consumer_lag{group,topic,partition}` from librdkafka statistics every 5 seconds; `psim_kafka_rebalances_total`, `psim_kafka_retries_total`, `psim_kafka_dead_letters_total{stage,code}`, `psim_kafka_transactions_total{result}`, `psim_kafka_stopped_partitions`.
9. **librdkafka build** in Conan profiles under `tools/conan/profiles`:
   - librdkafka 2.14 `rd_atomic32_set`/`rd_atomic64_set` check `HAVE_ATOMICS_{32,64}_ATOMIC`, which its build does not define. They become ordinary writes, causing a TSan-detected data race. Define these macros through compiler flags in every profile; include the flags in the package id;
   - ThreadSanitizer does not intercept glibc C11 threads (`thrd_create`, `mtx_*`, `cnd_*`). The TSan profile disables librdkafka C11 detection by renaming `thrd_create`, selecting its pthreads implementation;
   - disable `readability-redundant-member-init` in the clang-tidy profile (ADR-033): `{}` on a configuration structure member marks it optional for designated initialization; otherwise `-Wmissing-designated-field-initializers` requires every member.
10. **Pipeline verification:**
   - TS-03, `task test:component`: component tests use the CTest `component` label, are excluded from presets, and run in the toolchain container on the environment network. Coverage for modules marked `measured_by: component` in `tools/cpp/coverage.yaml` combines unit and component profiles;
   - FF-04, `task contracts:registry-compat`: register the latest release schemas (`master` before the first release), then working-tree schemas, under temporary subject prefixes;
   - B-01, `task bench:kafka`, runs nightly;
   - all three run in `env:ci-test`; Kafka layer and contract changes also enable the `env` pipeline stage.

## Consequences

- PostgreSQL -> Kafka component tests under TSan found an `rkcg_flags` race between heartbeat and queue reads in librdkafka 2.14.2. The [local recipe](../../tools/conan/recipes/librdkafka/conanfile.py) keeps that version and applies the [atomic flags patch](../../tools/conan/recipes/librdkafka/patches/0005-Atomic-consumer-group-flags.patch) in every build. KIP-848 and the sanitizer remain enabled.
- This refines ADR-006 and crosscutting.md 3.3: one transactional producer per shard instead of per partition, with the same exactly-once guarantee.
- Recovery after instance failure depends on the group session timeout; the installer configures it for stages with strict latency budgets.
- Processing within a shard is sequential; shard and partition counts provide parallelism (ADR-016).
- Event processing and response services receive `Processor` and implement only their handler.

## Verification

- Unit tests cover wire format, serde, retry backoff, configuration and delivery errors without brokers, and the registry client against a fake HTTP server.
- Component tests (TS-03):
  - delivery of keys, values, headers and trace context;
  - broker connection loss through Toxiproxy without loss or duplicates;
  - `read_committed` hides aborted transactions;
  - retries, DLQ and circuit breaker;
  - rebalancing without loss;
  - serde with a registry schema;
  - SIGKILL at random processing points: EOS-K produces each message exactly once; ALO loses none.
- B-01 measures throughput on Linux with native Docker Engine.
