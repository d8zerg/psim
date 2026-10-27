# ADR-016. Execution threads and coroutines

<!-- language-links -->
[English](0016-threading-model.md) | [Русский](../ru/adr/0016-threading-model.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-009, ADR-014, ADR-015, ADR-039 (service runtime), [ADR-040](0040-async-primitives.md) (implementation)

## Context

Services process tens of thousands of messages per second with latency budgets of tens of milliseconds. They use Kafka, PostgreSQL, Redis, gRPC and HTTP, and need cancellation, deadlines, backpressure and deterministic testing. All services need a shared concurrency model.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| One thread per connection or request, blocking I/O | Simple code | Scaling and latency limits; races on shared state |
| Shared `io_context` on a thread pool with strands | Standard Asio approach | Partition state needs synchronization; task migration between threads reduces cache locality |
| **One thread per core (shard): one `io_context` per thread, with partitions and connections assigned to that thread; C++20 coroutines (`asio::awaitable`); a separate pool for blocking operations** | No locks on the hot path: one thread owns partition state; predictable latency; natural backpressure | Uneven shard load, balanced by partition assignment |
| External frameworks (Seastar, userver) | Existing implementations | Seastar requires its own stack and OS configuration; userver is a large framework with an ecosystem that conflicts with the selected stack |

## Decision

1. **Shards:** N execution threads, each with its own `io_context`. `runtime.shards` configures N; the default 0 selects the cores available to the process through affinity and cgroups. An assigned Kafka partition, its state (RocksDB and tracker) and its transactional producer belong to one shard. HTTP and gRPC connections are distributed among shards. Keys map consistently to shards (`Shards::index_for`).
2. **Control loop:** a separate runtime thread (ADR-039) handles administrative HTTP, signals and component lifecycle. Shards start before components and stop after components drain.
3. **Coroutines:** use `asio::awaitable` and `co_await` throughout asynchronous code. Cancellation uses Asio cancellation signals (`asio::cancellation_signal`, with cancellation of the losing `||` branch). Deadlines use `with_deadline(clock, timeout, operation)` and `COMMON_DEADLINE_EXCEEDED`.
4. **Blocking operations** (large RocksDB batches, SDK file I/O, DNS, synchronous clients) run in a bounded `BlockingPool`. Both thread count and running plus queued tasks are bounded. The caller waits for capacity, providing backpressure; the result returns to its shard. Library exceptions become `COMMON_INTERNAL`.
5. **librdkafka:** redirect the client event queue (`rd_kafka_queue_io_event_enable`) to an eventfd monitored by Asio, without a dedicated polling thread.
6. **gRPC:** the asynchronous callback API runs on gRPC threads; `asio::post` transfers execution to the shard.
7. **Backpressure** (`libs/platform/async`, SR-18): `BoundedQueue` makes producers wait for space; `Credits` grants batches and consumes on demand with strict FIFO and no starvation; `Semaphore` provides RAII permits. Closing wakes all waiters.
8. **Time:** the `Clock` port provides event time (`wall`), monotonic time (`steady`) and waiting (`sleep_until`). `SystemClock` uses Asio timers; `ManualClock` advances only through `advance`, supporting deterministic tests and ADR-044 simulation.
9. OpenTelemetry **trace context** is thread-local, while shard coroutines interleave. Therefore, do not hold a `Scope` across `co_await`: pass the parent span explicitly (`StartSpan(..., {.parent = ...})`).

## Consequences

- Primitives belong to one shard and are used by coroutines on one `io_context`; cross-shard calls use only `asio::post` and `Shards::post`.
- The domain core is synchronous and pure. Asynchronous code belongs only in application and adapter layers.
- Shards communicate only through messages (`asio::post`), with no shared mutable state.

## Verification

TSan tests and primitive-overhead benchmarks establish a baseline.
