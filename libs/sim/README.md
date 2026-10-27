# Deterministic Simulation Environment

<!-- language-links -->
[English](README.md) | [Русский](../../docs/ru/repository/libs/sim/README.md)
<!-- /language-links -->

[ADR-044](../../docs/adr/0044-deterministic-simulation.md) · `psim::sim`

The environment verifies delivery semantics in a single process without external infrastructure. It is launched only manually through Task; it does not participate in `check`, CI, night pipeline, or release. CMake excludes it from the regular build, and no tests are registered in CTest.

## Launch

```bash
task sim:test
task sim:short
task sim:long
task sim:replay SEED=42
task sim:sanitizers
task sim:coverage
```

`sim:short` checks delivery and correlation on 200 consecutive seeds starting from `SEED` (default is 1), and `sim:long` on 10,000 seeds. `sim:replay` runs one seed and saves the complete trace. `sim:test` separately checks models, failure boundaries, oracle, and CLI. Commands build required targets in the project’s toolchain; Kafka/PostgreSQL/Valkey networks are not required.

General parameters: `PRESET=debug|release|asan|tsan|coverage`, `SEED=1`, `EVENTS=24`, `PARTITIONS=3`, `FAULT_DECISIONS=120`, `MAX_HANDLERS=100000`, `MAX_TIME_MS=30000`, `MODEL_VERSION=1`. Time limit applies to virtual time. Random failures are stopped after a certain number of decisions, then the pipeline must be drained; infinite unavailability is checked with a separate regression scenario with a processing limit.

```bash
task sim:replay SEED=42 EVENTS=48 PARTITIONS=4 FAULT_DECISIONS=300
task sim:short PRESET=release SEED=201
```

CLI `build/debian13/<preset>/libs/sim/psim-sim --help` allows specifying `--runs`, `--trace-dir`, and `--save-trace`. Return values: 0 - verification succeeded, 1 - scenario did not match or oracle detected an error, 2 - incorrect arguments/version of the model or trace recording error. Seed range overflow is rejected.

## Models and Chain

| Component | Behavior |
|---|---|
| `Random` | SplitMix64 and rejection sampling, reproducible decisions independent of STL distributions |
| `Scheduler` | One Asio io_context, `ManualClock`, advancing to the next timer, limits, and detecting deadlock |
| `Broker` | Stable partition logs, read_committed, atomic output/changelog/offsets, fencing, and single commit |
| `Store` | Versioning transaction inbox + effect + outbox, isolation of `(tenant, id)`, separate confirmation of publication |
| `run_pipeline` | Standard delivery, scenario failures, independent oracle, trace, and stable fingerprint |

Input is recorded in a stable log, duplicated, and shuffled. The transaction processor publishes normalized output and changelog along with offsets; after a crash, it restores deduplication from the changelog. The next processor stores inbox and outbox in the database before the offset is confirmed. The relay publishes outbox; a crash after publication creates a duplicate delivery. The recipient uses its inbox to prevent duplicate effects.

Lost read acknowledgments leave a record in the log, lost acknowledgments require repetition. Failure before commit cancels unfinished changes; failure after commit preserves them. Delays change the order of partitions; the order of the stable log within a partition is preserved. The model does not claim to restore physically lost confirmed data.

The oracle compares keys and content with an independent input set: no lost, redundant, or corrupted messages, transaction output is unique, database and recipient effects match expectations. Full log consumption and absence of unfinished outbox are checked. Transport duplicates are allowed and considered separately.

## Reproduction and Expansion

If the CLI fails, it prints the exact command `sim:replay` with seeds and all parameters, saving `.cache/sim/v1-seed-<seed>.trace`. In the header, there is the model version, parameters, and fingerprint, followed by inputs, decisions, virtual moments, commit, and oracle result. The file of one seed is overwritten; before changing the model, save the regression example in the repository and record the source code. Same versions, parameters, and seeds yield the same complete trace, even in case of failure.

`FaultScript` specifies failures at specific stages and points (before/after commit, publication, confirmation); it replaces random selection. The script and `Transform` must be deterministic and stored together with the regression test. `Transform` connects the processor to the standard chain, whose oracle expects to save the original message; for other semantic contexts, a separate scenario and oracle are needed.

A separate correlation target uses the actual computing rules and model `Broker`: it compares signals with an independent expected set during crashes before commit, after commit, before updating the local copy, and when reassigning partitions. It also runs only manually, without CTest and CI.

```bash
task correlation:sim
task correlation:sim RUNS=10000
task correlation:sim SEED=42 RUNS=1 SAVE_TRACE=true
```

Simulation does not run real adapters and services, nor does it prove the absence of duplicate domain incidents. Replication, protocols, and physical failures are checked by component and chaos tests.

Manual coverage includes libraries and CLI; the threshold is 75% lines. Reports are located in `build/debian13/coverage/libs/sim/coverage.json` and `coverage-html/`.
