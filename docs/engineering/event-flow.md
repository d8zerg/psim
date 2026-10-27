# End-to-End Test Environment

<!-- language-links -->
[English](event-flow.md) | [Русский](../ru/engineering/event-flow.md)
<!-- /language-links -->

[ADR-047](../adr/0047-event-flow.md).

```text
Simulator -> Connector Gateway -> Kafka raw data -> Normalizer -> Kafka normalized data
-> Correlation Engine -> Kafka signals -> Incident Service + PostgreSQL outbox
-> Kafka incident events -> Projection Service -> PostgreSQL / Redis -> API Gateway -> browser
-> Kafka incident events -> API Gateway -> WebSocket -> browser
```

## Launch

```bash
task flow:up
task flow:emit COUNT=3
task flow:down
```

Open `http://localhost:4173`, sign in as `operator1` with password `psim-dev-Passw0rd`. The feed receives the initial snapshot and is updated via WebSocket. `flow:down` stops the application processes; the infrastructure and data remain intact. For a complete shutdown, use `task env:down`. Data deletion is only possible explicitly with `task env:reset`.

`compose.event-flow.yaml` is connected to the base Compose as an optional profile. Services use compiled C++ binaries and console from the workspace. `deploy/local/config/event-flow` defines one shard, test endpoints, and explicit plaintext permissions. PKI and secure password files for the database are stored in git-ignored `.cache/event-flow`; these are only development credentials. The Keycloak fixture also aligns with the existing database realm.

## End-to-End Verification

```bash
task env:up
task flow:test
# Full path with sanitizers:
task flow:sanitizers
```

In CI, the task uses the already running project infrastructure from `psim-ci`, starts and then stops its application processes. `test:component` adds this process to the coverage profiles; `flow:sanitizers` repeats it under ASan/UBSan and TSan. The browser shares network space with the console container and opens a loopback address: PKCE uses Web Crypto in the locally allowed context of the browser. Unsafe Chromium flags are not required.

Verifications include OIDC authentication, rejection of invalid or anonymous tokens, technical role failure in reading the feed, mTLS input, normalization, one rule, inbox/outbox, and DOM updates. After three warm-up events, 30 events occur every 200 ms, each sent twice. 60 raw, 30 normalized, 30 signal records, and 30 domain changes are expected for measurable events. Browser connection breaks are checked with resume; SIGKILL API resumes from Kafka on the restarted Gateway without additional REST snapshots. After restarting, another event is sent. RESTORE does not generate a signal.

Artifacts include `build/event-flow-test`: Playwright logs, initial events, rendering measurements, `baseline.json`, causal `trace.json`, and feed snapshot `incident-feed.png`. Verification preserves individual observations; p50/p99 are calculated based on the nearest-rank method on raw observations. All containers on the same host use the same host kernel clock; for a distributed test environment, [syncing](../quality/latency-measurement.md) is required.

Basic debug settings: 30 measurements, 5 events per second, maximum generator delay of 0.91 ms; p50/p99 are around 35/50 ms before the signal, and 66/80 ms before the browser. This measures the functional test environment on a single host, not SLO under load. For comparing subsequent changes, use the profile, parameters, and raw data from a specific `baseline.json`.

## Process Responsibilities

| Process | Minimum Implementation |
|---|---|
| simulator | `tools/event-flow/psim-flow-simulator`: real gRPC client, PKI fixture, open-loop scheduling, and repetition of identical events |
| connector-gateway | URI SAN from the known connector, Register, event, BatchAck after Kafka, Credits |
| normalizer | One source in the fixture catalog, ALARM/RESTORE, typed EventRecord |
| correlation-engine | Single-event alarm rule, deterministic signal_id |
| incident-service | Grouping within a 60-second window, version, inbox, aggregate, and outbox in one transaction |
| projection-service | Shared PostgreSQL projection, Redis cache, scoped GET `/v1/feed/incidents` |
| api-gateway | JWT, HTTPS to Projection Service, WebSocket `/v1/realtime` |
| console | PKCE login, snapshot, aggregated versions, and deltas without periodic REST polling |

Current snapshot ownership is specified in [ADR-059](../adr/0059-console-projections.md). The baseline scope is documented in ADR-047; service guides describe the current capabilities. Do not use the fixture catalog, shared local database user, or PKI for provisioning.
