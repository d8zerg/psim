# Verification matrix

Generated from [verification.yaml](verification.yaml) by `task docs:verification:gen`. Do not edit manually.
See [strategy.md](strategy.md). Parentheses identify the gate: `pr` blocks merging, `nightly` blocks the next release, and `release` blocks release; `on-demand` runs manually outside CI.

## 1. Summary

| Kind | pr | nightly | release | on-demand | Total |
|---|---|---|---|---|---|
| Test suites | 23 | 13 | 0 | 3 | 39 |
| Load tests | 0 | 3 | 8 | 0 | 11 |
| Soak tests | 0 | 0 | 1 | 0 | 1 |
| Chaos and failures | 0 | 5 | 10 | 0 | 15 |
| Benchmarks | 5 | 1 | 0 | 1 | 7 |
| Fitness functions | 19 | 1 | 2 | 0 | 22 |
| Security | 4 | 2 | 3 | 0 | 9 |
| Manual procedures | 0 | 0 | 5 | 0 | 5 |
| **Total** | 51 | 25 | 29 | 4 | **109** |

## 2. SLO goals

| ID | Goal | Target | Methods |
|---|---|---|---|
| G-01 | Throughput | 50,000 events/s sustained for 24 hours; 1.5x peak for 10 minutes | L-01 (nightly), L-02 (release), L-03 (release), L-04 (release), L-06 (release), B-01 (nightly), B-03 (pr) |
| G-02 | Event to signal latency | p99 <= 300 ms | L-02 (release), L-03 (release), FF-05 (release), B-03 (pr), B-04 (pr) |
| G-03 | Event to incident in the console latency | p99 <= 1 s | L-05 (release), TS-11 (pr), FF-05 (release), TS-31 (pr), TS-32 (pr) |
| G-04 | REST API response latency | p99 <= 200 ms for console read operations | L-05 (release) |
| G-05 | Data loss | 0 events and 0 incidents, verified by end-to-end sequence reconciliation | TS-07 (on-demand), TS-08 (on-demand), TS-28 (pr), L-03 (release), C-01 (release), C-02 (nightly), C-06 (release), C-07 (nightly), C-08 (release), C-10 (nightly), C-11 (release) |
| G-06 | Duplicate incidents | 0 across restarts and retries | TS-02 (pr), TS-07 (on-demand), TS-08 (on-demand), TS-28 (pr), C-02 (nightly), C-11 (release) |
| G-07 | Cluster recovery time objective | <= 60 s after failure of any one node | C-01 (release), C-02 (nightly), C-03 (release), C-11 (release), C-14 (release), C-15 (release) |
| G-08 | Incident recovery point objective | 0 in the cluster topology | C-03 (release), C-11 (release) |
| G-09 | Upgrades | No processing downtime in the cluster topology | C-12 (release) |
| G-10 | Console | Smooth incident feed with 10,000 active incidents | TS-15 (nightly), TS-14 (pr) |

## 3. Engineering quality goals

| ID | Goal | Target | Methods |
|---|---|---|---|
| G-11 | Compiler and clang-tidy warnings | 0; warnings are errors (`-Werror`) | FF-10 (pr) |
| G-12 | Sanitizers (ASan, UBSan, TSan) | Mandatory in CI, 0 findings | FF-11 (pr) |
| G-13 | Domain core coverage | >= 90% of lines and >= 80% of branches | FF-12 (pr), TS-01 (pr), TS-02 (pr) |
| G-14 | Infrastructure code coverage | >= 75% | FF-12 (pr), TS-03 (pr) |
| G-15 | Fuzzing | Rule parser and validator: 10 minutes without crashes in CI; all external parsers and the rule DSL: 24 hours per target without crashes for release qualification | SEC-09 (pr), SEC-04 (nightly), SEC-05 (release) |
| G-16 | Contract compatibility | Automatic CI verification without manual exceptions | TS-04 (pr), FF-04 (pr), TS-05 (nightly) |
| G-17 | Performance regression | Automatic CI gate: a regression greater than 5% blocks merging | B-05 (pr), B-02 (pr), FF-05 (release), L-01 (nightly) |
| G-18 | Builds | Reproducible, with SBOM and signed artifacts | FF-13 (nightly), FF-14 (release), FF-15 (pr), SEC-02 (pr) |
| G-19 | Documentation | Self-contained documentation of the implemented platform: arc42 and C4 architecture, ADRs, guides and API references; English by default and a complete Russian version | FF-08 (pr), FF-09 (pr), M-01 (release) |

## 4. Acceptance scenarios

| Scenario | Methods |
|---|---|
| [S1](../product/acceptance-spec.md) | TS-06 (pr), TS-10 (nightly), TS-11 (pr), M-01 (release), TS-22 (nightly), TS-24 (pr), TS-28 (pr), TS-29 (pr), TS-36 (pr) |
| [S2](../product/acceptance-spec.md) | TS-10 (nightly), TS-11 (pr), L-07 (nightly), TS-18 (nightly), TS-24 (pr), TS-29 (pr), TS-31 (pr), TS-36 (pr) |
| [S3](../product/acceptance-spec.md) | TS-09 (pr), TS-10 (nightly), L-02 (release) |
| [S4](../product/acceptance-spec.md) | TS-07 (on-demand), TS-10 (nightly), L-02 (release), TS-20 (nightly), TS-23 (nightly) |
| [S5](../product/acceptance-spec.md) | TS-02 (pr), TS-07 (on-demand), TS-08 (on-demand), C-02 (nightly), TS-28 (pr), TS-29 (pr) |
| [S6](../product/acceptance-spec.md) | L-05 (release), TS-11 (pr), TS-12 (pr), TS-19 (nightly), TS-32 (pr), TS-35 (on-demand) |
| [S7](../product/acceptance-spec.md) | TS-10 (nightly), TS-11 (pr), TS-17 (nightly), L-08 (release), TS-21 (nightly), TS-28 (pr), TS-29 (pr), TS-35 (on-demand) |
| [S8](../product/acceptance-spec.md) | TS-10 (nightly), TS-11 (pr), TS-12 (pr), C-10 (nightly), TS-23 (nightly), TS-29 (pr), TS-35 (on-demand) |
| [S9](../product/acceptance-spec.md) | TS-08 (on-demand), TS-10 (nightly), TS-11 (pr), TS-17 (nightly), TS-21 (nightly), TS-28 (pr), TS-35 (on-demand) |
| [S10](../product/acceptance-spec.md) | TS-13 (pr), TS-10 (nightly), TS-11 (pr), TS-20 (nightly), TS-21 (nightly), TS-22 (nightly), TS-23 (nightly), TS-24 (pr), TS-28 (pr), TS-37 (pr) |
| [S11](../product/acceptance-spec.md) | C-01 (release), C-02 (nightly), C-03 (release), C-04 (release), C-05 (release), C-06 (release), C-07 (nightly), C-08 (release), C-09 (nightly), C-10 (nightly), C-11 (release), C-15 (release), TS-14 (pr), TS-18 (nightly), TS-19 (nightly), TS-22 (nightly), TS-24 (pr), TS-32 (pr) |
| [S12](../product/acceptance-spec.md) | C-12 (release) |
| [S13](../product/acceptance-spec.md) | L-09 (nightly), TS-30 (pr), TS-10 (nightly), TS-11 (pr), M-05 (release), TS-37 (pr) |

## 5. Method catalog

### Test suites

| ID | Method | Pass criterion | Environment | Gate | Command |
|---|---|---|---|---|---|
| TS-39 | Observability package | Seven static dashboards in both languages, valid PromQL and configurations, alert transitions and instructions for C-01-C-15, business aggregates without access to source tables, mTLS metrics, OTLP logs and traces, Prometheus -> Alertmanager -> journal delivery, and browser rendering without query errors. Purpose: Delivery of observability assets and failure diagnostic procedures; does not replace RPO/RTO qualification. | E-CI | pr | `task deploy:observability:test` |
| TS-38 | Web module loading and recovery | Compare the original build, static modules, deferred editors, language dictionaries and separate entry points; ru/en, real cold and warm HTTP caches, complete graphs including dynamic imports and dictionary data, no premature loading of restricted routes, chunk failure, artifact replacement and translation reload with input preserved. Purpose: Compare loading strategies and verify current limits before changing the distribution. | E-CI | pr | `task web:loading` |
| TS-37 | Audit and supervisor reporting through the public interface | Real PKCE with TOTP, audit search and pagination, independent verification of downloaded JSONL, exact detection of modified and deleted PostgreSQL records, denial of access to another site, role revocation with the previous JWT, reference metrics for 30 days in under three seconds and matching CSV. | E-LOCAL | pr | `task insights:test` |
| TS-36 | Administration through the public interface | Real PKCE with MFA, certificate registration and renewal, mapping for a new connector type, hierarchy and import, maintenance, SLA, a rule with a trial run, scenario publication, and rejection of access to another site and invalid uploads. | E-LOCAL | pr | `task admin:test` |
| TS-35 | Operator console | Real PKCE, incident acceptance, checklist and decisions, command preview and execution, SLA and notifications, private floor plan and responsibility handover. | E-LOCAL | on-demand | `task operator:test` |
| TS-34 | Shell session and role-based routes | PKCE, ID token validation, a single concurrent refresh, cleanup on logout, direct entry, role boundaries and late responses. Purpose: Verify session isolation and the shared shell without mixing users' operational data. | E-CI | pr | `task web:check` |
| TS-33 | Server operations for the operational interface | Independent alarm, permissions, versions, retries, leases across instances, history, exercise isolation and authorized assistant sources. Purpose: Verify server operations for the operational interface without a read-model simulator. | E-CI | pr | `task operations:test` |
| TS-01 | Domain core unit tests | All tests pass; every invariant in aggregates.md and every transition in state-models.md is covered. | E-CI | pr | `task cpp:test` |
| TS-02 | Property-based aggregate invariant tests | Invariants hold for random command sequences; 0 duplicate incidents when signals are repeated. | E-CI | pr | `task cpp:test:property` |
| TS-03 | Adapter component tests against containers | Real Kafka, PostgreSQL, Redis and ClickHouse containers in the local environment; connection loss and failover scenarios. | E-CI | pr | `task test:component` |
| TS-04 | Contract validation | task contracts:check passes; formatting, linting, buf breaking, valid and current OpenAPI and AsyncAPI, consistent registries. | E-CI | pr | `task contracts:check` |
| TS-05 | REST contract tests against OpenAPI | All Protobuf routes are available through Gateway; actual responses conform to psim-api-v1.yaml, catalog error codes and RFC 9457. | E-CI | nightly | `task test:api-contract` |
| TS-06 | Connector protocol conformance suite | Reference connectors and the simulator pass the complete suite; a 10-minute connection outage causes no data loss. | E-CI | pr | `task connectors:conformance` |
| TS-07 | Short pipeline simulation | Manually run 200 trials with random failures; in-memory reconciliation finds 0 losses and 0 duplicate effects; reference delivery and correlation evaluator with independent oracles (ADR-044). | E-DEV | on-demand | `task sim:short` |
| TS-08 | Long pipeline simulation | Manually run 10 000 trials; reproduce failures from seed, model version and parameters; retain the trace for regression testing (ADR-044). | E-DEV | on-demand | `task sim:long` |
| TS-09 | End-to-end pipeline smoke test in Docker Compose | A simulator event creates an incident visible through the API and realtime; one trace covers the entire path. | E-CI | pr | `task test:smoke` |
| TS-10 | Scenario acceptance tests through the API | Scenarios in acceptance-spec.md executable through the API and connector protocol, including negative checks. | E-CI | nightly | `task test:acceptance` |
| TS-11 | End-to-end Playwright UI tests | Scenarios S1, S2, S6-S10, S13 through the interface with real Keycloak, Kafka and services; required steps, restart with scenario version preserved, second command approval, responsibility handover, incident resolution and closure. | E-CI | pr | `task web:native` |
| TS-12 | Role x action x scope authorization matrix | All actions and six roles, tenant, site and zone; service and Gateway requests, compact scope sets, REST/WSS revocation before JWT expiry and audited denials. | E-CI | pr | `task test:authz` |
| TS-13 | Audit integrity test | Verification detects database record modification, deletion and insertion with record-level precision. | E-CI | pr | `task test:audit-integrity` |
| TS-14 | Console resilience to connection loss | Recover from an outage of at least 5 minutes without losing changes; an expired JWT receives 401 after clock tolerance, and the new token differs from the old one; replay and Gateway restart, RESYNC outside the replay window. | E-CI | pr | `task web:session` |
| TS-15 | Console feed performance | 10 000 active incidents, scrolling >= 50 frames/s, delta application without blocking the interface. | E-CI | nightly | `task web:perf` |
| TS-16 | Automated interface accessibility check | axe-core reports no critical violations on key screens. Purpose: Accessibility of key screens. | E-CI | nightly | `task web:a11y` |
| TS-17 | Scenario and timer resumption after restart | Restart Response Engine during a scenario and before a timer fires; timer error <= 1 s. | E-CI | nightly | `task test:timers` |
| TS-30 | Incident facts and reporting | Metrics match reference values; retries and recomputation preserve results; roles and access scopes are enforced. Purpose: Metrics, authorization, retries and fact recomputation for the reporting API. | E-LOCAL | pr | `task reports:test` |
| TS-18 | Floor plan files and recovery | Upload PNG/JPEG up to 20 MB; reject forged file types and pixel-limit violations; deny other access scopes and public bucket access; a failed replacement preserves the version; restored objects and links match by SHA-256. Purpose: Operational criteria in ADR-042. | E-CI | nightly | `task test:acceptance` |
| TS-19 | Reliable device state and realtime updates | Offline after three missed heartbeats; historical delivery and old snapshots cannot improve freshness; reconciliation restores actual; snapshots and deltas after disconnection preserve versions and access scope; delivery p99 <= 200 ms. Purpose: Operational criteria in ADR-042. | E-CI | nightly | `task test:acceptance` |
| TS-20 | Maintenance mode and completion | Selected signals are suppressed and events retained; fire events and P1 pass through; reject other access scopes; preserve open-incident SLAs; completion within 1 s, including restart recovery. Purpose: Operational criteria in ADR-042. | E-CI | nightly | `task test:acceptance` |
| TS-21 | Responsibility handover and duty assignment | Recipient confirmation, version and scope checks, idempotence; the previous owner remains responsible until acceptance; preserve SLA and scenario; use the current duty assignee or fallback contact; audit actions. Purpose: Operational criteria in ADR-042. | E-CI | nightly | `task test:acceptance` |
| TS-22 | Certificate lifecycle | Initial registration through CSR; overlapping renewal without event loss; revocation closes the session and blocks reconnection; revoked certificates remain denied after recovery. Purpose: Operational criteria in ADR-042. | E-CI | nightly | `task test:acceptance` |
| TS-23 | Diagnostic replay isolation | History containing commands, email and webhooks causes 0 changes to production signals, incidents, commands and notifications and 0 external calls; ACLs reject production writes; audit scope and run parameters. Purpose: Operational criteria in ADR-042. | E-CI | nightly | `task test:acceptance` |
| TS-25 | Rule lifecycle and isolated historical replay | Two instances maintain consistent versions, retries and audit; invalid rules cannot activate; all five rule types produce identical decisions from Kafka and ClickHouse; maintenance preserves explanations and versions, while fire events and P1 pass through; jobs recover after SIGKILL; reject other access scopes; production topics remain unchanged and real ACLs prohibit writes and offset commits. Purpose: Real PostgreSQL, Kafka, Schema Registry, ClickHouse, Keycloak and two Correlation Engine instances. | E-CI | pr | `task rules:test` |
| TS-24 | Resource Catalog resources, floor plans and maintenance | Hierarchy and sources preserve invariants; versions and request retries remain consistent; reject other access scopes; PNG/JPEG up to 20 MiB and floor plan versions survive S3 and database failures; clean up orphans; maintenance ends within 1 s, including startup recovery; the audit consumer receives every change and access denial. Purpose: Two real instances, PostgreSQL, Kafka and S3; backup and suppression application are tested separately. | E-CI | pr | `task catalog:test` |
| TS-26 | Projection rebuild from Kafka | Projections rebuilt by Kafka replay match production projections; old events cannot produce false online or actual state; the end-to-end event scenario passes after API Gateway projection migration. Purpose: Reproducibility of read models from Kafka. | E-CI | nightly | `task test:projections` |
| TS-27 | Notification delivery and webhook protection | Email and webhooks reach test SMTP and HTTP receivers; receiver failure and recovery cause no notification loss or duplication; reject prohibited and internal webhook addresses; audit delivery. Purpose: Notification delivery and SSRF protection. | E-CI | nightly | `task test:notifications` |
| TS-28 | Incident Service and authorization | Two HTTPS instances, real Kafka, PostgreSQL and Keycloak; transitions, versions, retries, grouping window, history, access scope and confirmed handover preserve invariants; SIGKILL and repeated signals cause no lost or duplicate effects. Purpose: Incident lifecycle, shared authorization and responsibility preservation. | E-CI | pr | `task incidents:test` |
| TS-29 | Event history and incident links | Real ingestion workers, Kafka, ClickHouse and PostgreSQL; search and statistics enforce access scope; signals extend TTL; SIGKILL and retries preserve the full link set without logical duplicates. Purpose: Complete incident context, search and durable retention extension. | E-CI | pr | `task history:test` |
| TS-31 | Shared console models and Kafka recovery | Real Kafka, PostgreSQL, Valkey and HTTPS; access scopes, independent observation and report cursors, duty, maintenance, PostgreSQL fallback, rebuild equality and rejection of incomplete history; local update p99 <= 100 ms. Purpose: Consistent console snapshots, state freshness and safe recovery. | E-CI | pr | `task projection:test` |
| TS-32 | Realtime subscriptions, recovery and 500 clients | Two Gateways, 500 WSS clients and 10000 deliveries without loss; local p99 <= 200 ms; restore 500 subscriptions after a node restart without a new snapshot; filters, JWT, old clients, multiple subscriptions, private variants, current state after lease expiry and slow-consumer disconnection. Purpose: Compatible change delivery, node-independent resumption and bounded queues. | E-CI | pr | `task realtime:test` |

### Load tests

| ID | Method | Pass criterion | Environment | Gate | Command |
|---|---|---|---|---|---|
| L-01 | Nightly LOAD-5K test | Lag does not grow; stage latency regresses by no more than 5% against baseline; reconciliation finds no loss. | E-CI | nightly | `task load:nightly` |
| L-02 | Sustained LOAD-50K for 1 hour | 50 000 events/s; event -> signal p99 <= 300 ms; event -> console p99 <= 1 s; reconciliation finds no loss or duplicates. | E-CLUSTER | release | `task load:steady` |
| L-04 | 1.5x peak (75 000 events/s) for 10 minutes | 0 losses; lag returns to normal within 5 minutes after the peak. | E-CLUSTER | release | `task load:peak` |
| L-05 | Consoles and REST under load | 500 consoles; REST read p99 <= 200 ms; realtime delivery p99 <= 200 ms; event -> rendering p99 <= 1 s during LOAD-50K. | E-CLUSTER | release | `task load:consoles` |
| L-06 | Ingestion through two gateway instances | 50 000 events/s through 2 Connector Gateway instances up to Kafka acknowledgement; other application processes on the combined local profile are temporarily stopped; instance failure causes no loss. | E-CLUSTER | release | `task deploy:gateway:qualify` |
| L-07 | Import of 50 000 devices | Import completes within 5 minutes; consumers receive catalog changes within 2 s. | E-LOCAL | nightly | `task catalog:bench` |
| L-08 | Event history search under write load | Search one day of data for a zone within 500 ms while writing 50 000 events/s to ClickHouse. | E-CLUSTER | release | `task history:bench` |
| L-09 | Report covering 30 days | Report generation takes <= 3 s; values match the reference. | E-LOCAL | nightly | `task reports:bench` |
| L-10 | Single-machine LOAD-10K | 10 000 events/s on the reference server with cluster latency SLOs. Purpose: Target load for the single-machine topology (scaling.md, 5.4). | E-LOCAL | release | `task load:single-node` |
| L-11 | Incident Service signal processing | Two instances; signal processing p99 <= 50 ms; signal and incident reconciliation finds no loss or duplicates after an instance receives SIGKILL. Purpose: Incident stage latency budget (signal processing). | E-LOCAL | release | `task incidents:bench` |
| L-12 | Command delivery to the connector | Command request -> connector delivery p99 <= 200 ms; every command reaches a final status. Purpose: Command latency budget (command delivery). | E-LOCAL | release | `task commands:bench` |

### Soak tests

| ID | Method | Pass criterion | Environment | Gate | Command |
|---|---|---|---|---|---|
| L-03 | LOAD-50K soak test for 24 hours | The L-02 thresholds hold throughout the interval; memory use, lag and latency do not grow. | E-CLUSTER | release | `task load:soak` |

### Chaos and failures

| ID | Method | Pass criterion | Environment | Gate | Command |
|---|---|---|---|---|---|
| C-01 | Kafka broker failure | Under LOAD-50K, reconciliation finds no loss or duplicates; processing continues; RTO <= 60 s. | E-CLUSTER | release | `task chaos:kafka-broker` |
| C-02 | Termination of an instance of each service | Run kill -9 against each service in turn under load; reconciliation finds no loss or duplicates; RTO <= 60 s. | E-CI | nightly | `task chaos:service-kill` |
| C-03 | PostgreSQL primary failover | Patroni leader failure under load; 0 lost incidents (RPO = 0); failover <= 30 s. | E-CLUSTER | release | `task chaos:postgres-failover` |
| C-04 | Redis-compatible storage primary failure | Event processing continues; connector sessions recover. | E-CLUSTER | release | `task chaos:redis` |
| C-05 | ClickHouse replica failure | Search and reports use the second replica; ingestion catches up without loss after recovery. | E-CLUSTER | release | `task chaos:clickhouse` |
| C-06 | Network partition between an application node and infrastructure nodes | Available instances take over partitions; reconciliation after recovery finds no loss or duplicates. | E-CLUSTER | release | `task chaos:partition` |
| C-07 | Packet delays and loss | Toxiproxy injects 200 ms latency and 5% packet loss on Kafka and PostgreSQL connections; no data loss or duplicate effects; record latency degradation in the report. | E-CI | nightly | `task chaos:network` |
| C-08 | Disk exhaustion | Alert before processing stops; a full disk applies backpressure without losing acknowledged data. | E-CLUSTER | release | `task chaos:disk-full` |
| C-09 | Node and source clock skew | A source clock is 10 minutes ahead; events are corrected and marked clock_skew; node clock skew raises an alert. | E-CI | nightly | `task chaos:clock-skew` |
| C-10 | Connector connection loss for 10 minutes | SDK-buffered events are delivered without loss; commands issued during the outage reach a final status. | E-CI | nightly | `task chaos:connector-disconnect` |
| C-11 | Cluster node power loss | Measure RTO <= 60 s and RPO = 0 for each node in turn; reconciliation finds no loss. | E-CLUSTER | release | `task chaos:node-down` |
| C-12 | N -> N+1 upgrade and rollback under load | No interval longer than 5 s without new signals; latency SLOs hold throughout; reconciliation finds no loss or duplicates. | E-CLUSTER | release | `task deploy:upgrade:qualify` |
| C-13 | Keycloak unavailability | Existing sessions continue; new sign-ins receive a clear error; event processing is unaffected. Purpose: Threat T38 in the threat model. | E-CI | nightly | `task chaos:idp` |
| C-14 | Correlation state recovery | Correlation Engine recovers within 30 s after crashing with the reference state size. | E-CLUSTER | release | `task chaos:correlation-restore` |
| C-15 | File storage node failures | Stop the SeaweedFS volume server, filer, S3 gateway and master leader during floor plan reads and uploads; existing plans remain readable, failed uploads preserve the previous version, volume replicas recover and SHA-256 matches for every version. Purpose: Cluster floor plan storage resilience (ADR-042, ADR-046). | E-CLUSTER | release | `task chaos:filestore` |

### Benchmarks

| ID | Method | Pass criterion | Environment | Gate | Command |
|---|---|---|---|---|---|
| B-07 | Virtualized operator feed | 10000 incidents, scrolling at least 50 frames/s, realtime -> DOM p99 at most 1 s; raw measurement series and one browser worker. Purpose: Verify responsiveness of the operational feed without substituting for the full ingestion pipeline SLO. | E-CI | on-demand | `task operator:benchmark` |
| B-01 | Kafka layer | At least 200 000 messages/s for both producer and consumer on the reference environment. | E-LOCAL | nightly | `task bench:kafka` |
| B-02 | Asynchronous primitives | Record overhead as the baseline. | E-CI | pr | `task bench:async` |
| B-03 | Normalizer per CPU core | Record per-core throughput and stage p99 <= 30 ms as the baseline. | E-CI | pr | `task bench:normalizer` |
| B-04 | Correlation rule evaluation | Throughput for every rule type and cost of CEL expression evaluation. | E-CI | pr | `task bench:rules` |
| B-05 | Performance regression gate | Compare all benchmarks against the master baseline; regression greater than 5% blocks merging. | E-CI | pr | `task bench:compare` |
| B-06 | Reference simulator event generator | At least 75 000 durably stored events/s from one machine; the TLS receiver reconciles every sequence number and source ordering. Purpose: Verify generator performance independently of the platform pipeline. | E-LOCAL | pr | `task connectors:bench` |

### Fitness functions

| ID | Method | Pass criterion | Environment | Gate | Command |
|---|---|---|---|---|---|
| FF-01 | Module dependency directions | libs/domain has no infrastructure dependencies; services do not depend on each other (ADR-001). Purpose: Architecture rules (ADR-001). | E-CI | pr | `task arch:deps` |
| FF-02 | Domain core purity | libs/domain prohibits system clocks, randomness, input/output and exceptions as the domain error mechanism. Purpose: Deterministic domain core. | E-CI | pr | `task arch:domain-purity` |
| FF-03 | tenant_id in all storage queries | Static checks of repository SQL and ClickHouse queries (ADR-022). Purpose: Multitenancy (ADR-022). | E-CI | pr | `task arch:tenant` |
| FF-04 | Schema Registry compatibility | Register working-tree schemas against the latest release using ADR-004 compatibility rules. | E-CI | pr | `task contracts:registry-compat` |
| FF-05 | Per-stage latency budgets | Each stage's p99 in load tests stays within its budget in quality.md, section 4. | E-CLUSTER | release | `task load:budget` |
| FF-06 | Service observability | After startup, each service serves /health/live, /health/ready and /metrics and exports traces. Purpose: Metrics, logs and traces for every service. | E-CI | pr | `task test:observability` |
| FF-07 | Prohibit concatenation of user input into queries | Static analysis (requirement SR-13). Purpose: Security requirement SR-13. | E-CI | pr | `task arch:sql` |
| FF-08 | Documentation as code | task docs:check passes; links, typography, PlantUML, C4, threat and verification registries are current; both language versions are complete and reviewed. | E-CI | pr | `task docs:check` |
| FF-09 | Documentation portal build | English is the default; complete English and Russian portals build from maintained sources and generated OpenAPI, AsyncAPI and Protobuf references; offline archive, local search and section-preserving language switches pass browser checks. | E-CI | pr | `task docs:portal` |
| FF-10 | Zero compiler and clang-tidy warnings | Build with -Werror; clang-tidy reports no findings under the project profile. | E-CI | pr | `task cpp:lint` |
| FF-11 | Sanitizers | Unit, property and component tests pass under ASan+UBSan and TSan without findings. | E-CI | pr | `task cpp:test:sanitizers` |
| FF-12 | Coverage thresholds | Domain core coverage >= 90% of lines and >= 80% of branches; infrastructure coverage >= 75% of lines. | E-CI | pr | `task cpp:coverage` |
| FF-13 | Build reproducibility | Two independent builds of the same commit produce identical artifacts. | E-CI | nightly | `task cpp:reproducible` |
| FF-14 | SBOM and artifact signatures | Every release artifact has a CycloneDX SBOM and a signature verified by the installer. | E-CI | release | `task release:verify` |
| FF-15 | Artifact execution on the target platform | Test executables and psimctl pass on Debian 13 x86_64 without the toolchain; all system shared-library dependencies resolve (ADR-032). | E-CI | pr | `task cpp:runtime-test` |
| FF-16 | C++ formatting | clang-format with .clang-format changes no project files. Purpose: Coding standard (ADR-033). | E-CI | pr | `task cpp:format` |
| FF-17 | Architecture check self-tests | For every arch:deps, arch:domain-purity and arch:sql rule, a violating fixture fails. Purpose: Verify architecture checks even with an empty domain core (ADR-033). | E-CI | pr | `task arch:selftest` |
| FF-18 | Package installation on the target platform | Each DEB package installs, runs and uninstalls in a clean Debian 13 x86_64 image; dpkg-shlibdeps derives dependencies from actual executable symbols. Purpose: Debian 13 x86_64 distribution (F-PLT-06, ADR-034). | E-CI | pr | `task release:package:test` |
| FF-19 | Images without root or unnecessary privileges | Every image runs as a non-root user with a read-only filesystem, no capabilities and no-new-privileges. Purpose: Requirement SR-24 (ADR-034). | E-CI | pr | `task release:images:test` |
| FF-20 | Local environment from scratch | Start a fresh environment within 5 minutes with 1 and 3 Kafka brokers; all component and inter-component smoke tests pass; Keycloak realm enforces SR-02. Purpose: Reproducible environment and requirement SR-02 (ADR-035). | E-CI | pr | `task env:ci-test` |
| FF-21 | Web client quality | Formatting, ESLint without warnings, strict type checks, Vitest coverage >= 80% per package, bundle budgets, REST and realtime clients generated from contracts, and an end-to-end console smoke test in Chromium. Purpose: TypeScript standard and web client checks (ADR-037). | E-CI | pr | `task web:check` |
| FF-22 | Shared service authorization | Services contain neither custom scope checks nor JWT validation without shared introspection configuration; negative self-tests detect violations. Purpose: Shared RBAC/ABAC implementation and mandatory identity freshness checks. | E-CI | pr | `task arch:authz` |

### Security

| ID | Method | Pass criterion | Environment | Gate | Command |
|---|---|---|---|---|---|
| SEC-01 | Static security analysis | clang-tidy profile with bugprone, cert and security checks; 0 findings. Purpose: Security requirements verified by scanning. | E-CI | pr | `task cpp:lint` |
| SEC-02 | Dependency vulnerabilities and licenses | Trivy scans lockfiles and SBOMs; critical and high vulnerabilities and prohibited licenses block the pipeline. | E-CI | pr | `task security:deps` |
| SEC-03 | Image vulnerability scanning | Trivy scans release images; critical and high vulnerabilities block the pipeline. Purpose: Requirements SR-23, SR-24. | E-CI | nightly | `task security:images` |
| SEC-04 | Nightly fuzzing | Every fuzz target runs for 1 hour without crashes; new crashing inputs are added to the corpus. | E-CI | nightly | `task fuzz:nightly` |
| SEC-05 | Fuzzing for 24 hours | Every untrusted-data parser and the rule DSL run for 24 hours without crashes. | E-LOCAL | release | `task fuzz:release` |
| SEC-09 | Rule parser and validator fuzzing for 10 minutes | Rule DSL parser and validator run for 10 minutes without crashes or sanitizer findings; retain the corpus and report. | E-LOCAL | pr | `task fuzz:rules` |
| SEC-06 | REST and WebSocket DAST | OWASP ZAP scans OpenAPI and realtime; no high-severity findings. Purpose: Security requirements verified by penetration testing and scanning. | E-LOCAL | release | `task security:dast` |
| SEC-07 | TLS configuration | testssl.sh checks external ports; only TLS 1.3 on TB1 and TB2, with no weak cipher suites. Purpose: Requirement SR-08. | E-LOCAL | release | `task security:tls` |
| SEC-08 | Platform trust and authentication | JWT/JWKS and key rotation, rejection of incorrect issuer/audience/algorithm, TLS/mTLS with CA/name/CRL verification, renewal without losing test events, revocation of an active stream, expiry and protected secret files; real Keycloak, Kafka, PostgreSQL and Valkey; ASan/UBSan and TSan. Purpose: Platform mechanisms for SR-01, SR-03, SR-04, SR-08, SR-09; the complete service lifecycle remains in TS-22. | E-CI | pr | `task test:security:sanitizers` |

### Manual procedures

| ID | Method | Pass criterion | Environment | Gate | Command |
|---|---|---|---|---|---|
| M-01 | Installation and connector integration by an independent engineer | Use each complete EN and RU version independently, with English as the default, without developer assistance or development-only materials; verify actual platform behavior and absence of direct and indirect dependencies on removed materials; record elapsed time and findings. | E-LOCAL | release, manual | procedure |
| M-02 | Backup recovery exercise | Restore into a clean environment using the procedure and measure elapsed time; floor plans, links and SHA-256 match, Keycloak and trust recover, and revoked keys cannot grant access. Purpose: Recovery from an independent backup. | E-LOCAL | release, manual | procedure |
| M-03 | Internal penetration test against the threat model | Test every high-risk threat in threat-register.md; no open critical or high-severity findings. Purpose: Security requirements verified by penetration testing. | E-CLUSTER | release, manual | procedure |
| M-04 | Reproduce the demonstration from scratch | Reproduce the demonstration environment and scenarios using the instructions. Purpose: Reproducible synthetic demonstration. | E-LOCAL | release, manual | procedure |
| M-05 | S1-S13 acceptance run | Final execution of every scenario, signed off by the product owner. | E-CLUSTER | release, manual | procedure |
