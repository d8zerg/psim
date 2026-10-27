# ADR-040. Asynchronous primitives of the platform

<!-- language-links -->
[English](0040-async-primitives.md) | [Русский](../ru/adr/0040-async-primitives.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-016](0016-threading-model.md) (thread model), [ADR-039](0039-service-runtime.md) (runtime), ADR-033 (clang-tidy profile), requirement SR-18, method B-02

## Context

ADR-016 chooses one thread per CPU core with Asio coroutines. These primitives implement that model: shards, deadlines and cancellation, backpressure, a blocking operations pool and a clock port. Acceptance requires TSan tests and an overhead benchmark baseline.

## Proposed Options

### Waiting in primitives

| Option | Advantages | Disadvantages |
|---|---|---|
| `asio::experimental::channel` as a queue and credit base | Ready primitive | No “n credits” waiting with strict FIFO; closing loses buffered elements |
| `concurrent_channel` and mutexes | Works between threads | Blocking on the hot path, contradicts ADR-016 |
| **List of steady timers (`steady_timer`) with infinite duration as an event within one `io_context`** | No blocking; cancellation in Asio works normally (cancelled waiting removes itself); strict FIFO | Primitives are only shard-local |

### Blocking operations pool

| Option | Advantages | Disadvantages |
|---|---|---|
| `asio::post` in `thread_pool` without limits | Simple | No backpressure: task queue grows infinitely |
| **`thread_pool` + `concurrent_channel` with tokens** | “Executed and waiting” are counted as one; token waiting is a coroutine, shard is not blocked | Inter-thread channel (acceptable on the cold path) |

## Solution

Library `libs/platform/async` (`psim::platform_async`), infrastructure layer:

1. **`Clock`** - `wall()`, `steady()`, `sleep_until()`/`sleep_for()` (coroutines; `false` - cancellation). `SystemClock` - Asio timers on the calling executor. `ManualClock` - `advance(d)` wakes up all whose time has expired, in order (time, priority), `advance_to_next()`, `sleepers()`.
2. **`with_deadline(Clock*, timeout, op)`** -> `Result<T>`: when the deadline is reached, the losing branch is cancelled, with an error `COMMON_DEADLINE_EXCEEDED`. `spawn_cancellable(executor, op)` returns a `Cancellable` - cancels a coroutine from outside through an Asio signal.
3. **Backpressure:** `Credits` (`grant(n)`, `try_acquire(n)`, `acquire(n)` - strict FIFO: large requests in the queue cannot be preempted by smaller ones; `close()` wakes up all with failure); `Semaphore` with RAII-`Permit`; `BoundedQueue<T>` (`push` waits for space, `pop` waits for an element; after `close()`, the queue returns remaining elements, then `nullopt`).
4. **`Shards`** - N `io_context` in N named threads (`psim-shard-<i>`), `executor(i)`, `index_for(key)`, `post`; `stop(grace)` causes the work to finish before the deadline, then stops. `available_cpus()` - based on process affinity.
5. **`BlockingPool(threads, max_tasks)`** - `run(fn)` -> `Result<T>`: waits for a token (backpressure), executes `fn` in the pool, returns result to the calling executor; exceptions -> `COMMON_INTERNAL`, `Result`-errors from `fn` are passed as they are; after `stop()`, `COMMON_UNAVAILABLE` failure.
6. **Runtime** (addition to ADR-039): the `runtime` section of configuration - `shards` (0 = `available_cpus()`), `blocking_threads` (4), `blocking_tasks` (64); `Context` provides components with `shards()`, `clock()`, `blocking()`; metric `psim_runtime_shards`. Shards start before components and stop after they are drained, then the pool.
7. **Code rules:** primitives are not thread-safe and are used by coroutines of one shard; references in coroutine parameters are prohibited (`cppcoreguidelines-avoid-reference-coroutine-parameters`) - dependencies are passed by pointer; OpenTelemetry `Scope` is not held through `co_await`.
8. **clang-tidy profile:** additionally disabled `clang-analyzer-core.NullDereference` - like `core.CallAndMessage` (ADR-039), the analyzer does not model C++20 coroutines and gives false results inside Asio; null dereference catches `-Wnull-dereference`, ASan and UBSan.

## Consequences

- Services receive shards, clocks, deadlines and a pool through `Context`; `ManualClock` makes time-dependent logic tests deterministic.
- Inter-shard interaction - only `Shards::post`/`asio::post`; no shared primitives between shards.
- Kafka and gRPC/HTTP adapters assign connections and partitions to shards through `index_for`.

## Verification

- Modular tests for all primitives (cancellation in the middle of waiting queues, closing, FIFO credits, deadline with `ManualClock`, stopping shards with grace, pool failure after stopping); ASan, UBSan, and TSan runs; coverage of `libs/platform/async` ≥ 75% of lines.
- B-02 - `task bench:async` on Linux with native Docker Engine (16 CPU); regression > 5% fails `bench:compare`. Reference measurements from the previous environment: coroutine resumption ~21 ns, credit operation ~26 ns, queue transfer ~76 ns, `with_deadline` ~2 microseconds, inter-shard round trip ~11 microseconds, call through `BlockingPool` ~24 microseconds. The native Docker Engine baseline requires a new measurement.
