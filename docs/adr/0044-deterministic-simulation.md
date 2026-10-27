# ADR-044. Deterministic Simulation Environment with Manual Execution

<!-- language-links -->
[English](0044-deterministic-simulation.md) | [Русский](../ru/adr/0044-deterministic-simulation.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-006, ADR-008, ADR-016, ADR-031, ADR-040, [Quality Strategy](../quality/strategy.md)

## Context

Simulation requires a reproducible environment with controlled timing, Kafka emulation, and storage systems, as well as fault injection and seed-based reproduction. Manual execution of the environment as separate tasks without integration with CI is accepted.

## Proposed Options

| Option | Advantages | Limitations |
|---|---|---|
| Real containers and network faults | Tests real adapters | Does not provide deterministic scheduling; already covered by component tests |
| Separate event engine without Asio | Simple timing control | Does not exercise coroutine execution through the existing Clock port |
| One io_context + ManualClock, durable state models | Uses ADR-040 asynchronous primitives; reproduces schedules, timers, and transaction boundaries | Does not model protocols, replication, and real processes |

## Solution

1. `libs/sim`, namespace `psim::sim`, target `psim::sim`. One `io_context`, existing `ManualClock`, moves to the next timer. Limits on handlers and virtual time terminate stuck scenarios with an error; system clocks and threads are not used.
2. SplitMix64 and a bounded sampler with rejection sampling. State iteration order is fixed; no `random_device`, standard distributions, or `std::hash`. The seed reproduces the same version of the model and parameters.
3. Kafka model: durable partition logs, read_committed, atomic publication of output/changelog/offsets, fencing by producer epoch, prohibition of repeated commits and offset rollback. SQL model: optimistic transaction inbox + effect + outbox; key `(tenant, id)`, state version, separate acknowledgment for outbox.
4. Reference chain: repeated and shuffled inputs -> Kafka EOS-K with state restoration from changelog -> inbox/outbox -> repeated publication -> receiver inbox. The failure loses unfinished transactions and local progress, while preserving commits; loss of response does not remove durable records. Delays change the interleaving of partitions while maintaining log order within the partition.
5. Independent oracle compares input set with output, database effects, and receiver, including content, tenant, and transaction output multiplicity; checks that logs and outbox are drained. Repeated transport publications are allowed, but repeated effects are excluded.
6. `task sim:short` - 200 iterations, `task sim:long` - 10,000, `task sim:replay SEED=<number>` - one iteration with trace. Errors save model version, parameters, schedule, and repetition command in `.cache/sim/`. For regression, fixed seeds and explicit fault scripts around commit/publishing/acknowledgment are used.
7. Only manual execution: CMake subdir has `EXCLUDE_FROM_ALL`; environment checks are not registered in CTest. Tasks `sim:*` are not triggered from `check`, `ci`, `ci:nightly`, or `release`. This clarifies only automatic simulation in ADR-006 and quality strategy; other barriers remain.

## Consequences

- Model libraries and schedulers can be used in future domain port tests. `Transform` allows connecting deterministic handlers to the standard chain; domain scenarios need their expected results.
- Simulation checks confirm the semantics of the reference delivery. It does not verify the readiness of Normalizer, Correlation Engine, Incident Service, real adapters, or fault tolerance of replication. The live correlation oracle is described in [guide](../engineering/live-correlation.md).
- Repetition between modified models requires a saved version of the source code; CLI rejects unknown model versions. Changing schedule algorithm, generator, or scenario format requires upgrading the version.
- Manual execution results are preserved as test artifacts; the environment is not an automatic release barrier.

## Verification

`task sim:test`, `task sim:short`, `task sim:long`, `task sim:sanitizers`, `task sim:coverage`. Manually check for absence of simulation tests in CTest and `sim:*` calls in pipelines. Data corruption and conflicts between output identifiers must be detected by the oracle; seed repetition should produce the same complete trace.
