# ADR-035. Local development and integration-test environment

<!-- language-links -->
[English](0035-local-environment.md) | [Русский](../ru/adr/0035-local-environment.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-002, ADR-003, ADR-015, ADR-028, ADR-031, ADR-034, requirements SR-02, SR-19 and SR-23 ([threat-register.md](../security/threat-register.md))

## Context

One command starts Kafka in KRaft mode with one or three brokers, Schema Registry, PostgreSQL, ClickHouse with Keeper, a Redis-compatible store, Keycloak with a configured realm, OpenTelemetry Collector, Prometheus, Grafana, log and trace stores, and fault-injection proxies. Startup from scratch to readiness must take no more than 5 minutes. Pipeline integration tests use this environment (ADR-034). It runs alongside the IDE and background pipeline on a Linux development host with native Docker Engine and the Docker Compose plugin, 16 CPUs and 32 GB RAM.

Existing choices are Kafka 4.x KRaft (ADR-002), PostgreSQL-backed Apicurio Registry 3.x (ADR-003), replicated ClickHouse engines and Keeper in all topologies (ADR-028), and Toxiproxy (ADR-031). ADR-080 defines release log and trace stores; ADR-015 defines the Redis-compatible server and client.

## Options considered

### Development logs and traces

| Option | Advantages | Disadvantages |
|---|---|---|
| Jaeger + VictoriaLogs | Apache 2.0 | Separate systems with weaker Grafana trace-to-log navigation |
| ClickHouse through a Collector exporter | Distribution candidate (ADR-028) | Requires custom schemas and Grafana queries |
| **Loki + Tempo, single processes with local disks** | Native Grafana sources, trace <-> log navigation through `trace_id`, OTLP ingestion, about 160 MB memory | ADR-080 defines distribution and licensing conditions |

### Redis-compatible store

Use **Valkey**, proposed by ADR-015, with BSD-3 licensing and Redis protocol support. ADR-015 defines topology and client.

### Three-broker variant

| Option | Advantages | Disadvantages |
|---|---|---|
| Two Compose files | Explicit | Duplicate broker definitions |
| **One file: `kafka-1` always enabled, `kafka-2` and `kafka-3` in profile `kafka3`; variables define quorum and replication** | One YAML-anchor broker definition; identical client addresses | Broker-count changes require a data reset because the KRaft quorum changes; the task enforces this |

### Environment access

Tests run inside the Compose network using service names; published ports serve human users only. A separate file publishes them only on `127.0.0.1`. Pipeline environments use their own project names without published ports and do not conflict with interactive environments.

## Decision

1. **Components** in `deploy/local/compose.yaml`, with tags and digests pinned under SR-23:

   | Component | Version | Role |
   |---|---|---|
   | Apache Kafka | 4.3.1 | KRaft broker/controller; 1 or 3 nodes |
   | Apicurio Registry | 3.3.3 | PostgreSQL-backed Schema Registry with Confluent-compatible API |
   | PostgreSQL | 18.6 | Databases `psim` for services, `registry` and `keycloak` |
   | ClickHouse, Keeper | 26.8 LTS | Cluster `psim`: one shard and one replica; macros allow the same DDL as the cluster |
   | Valkey | 9.1 | Redis-compatible store (ADR-015) |
   | Keycloak | 26.8 | Realm `psim`, described below |
   | OpenTelemetry Collector contrib | 0.162 | OTLP gRPC and HTTP; traces -> Tempo, logs -> Loki, metrics -> Prometheus remote write |
   | Prometheus | 3.15 | Collector and service metrics |
   | Loki, Tempo | 3.7, 3.1 | Development logs and traces |
   | Grafana | 13.2 | Prometheus, Loki and Tempo sources linked through `trace_id`; ADR-080 dashboards |
   | Toxiproxy | 2.12 | Kafka, PostgreSQL, Valkey and ClickHouse fault proxies (C-07) |

2. **Kafka listeners:** `INTERNAL` at `kafka-N:19092` for Compose services and tests; `FAULT`, advertised as `toxiproxy:29N92`, for the same broker through Toxiproxy, including fault-injected client metadata access; `EXTERNAL` at `localhost:9N92` for host tools. Topic auto-creation is disabled. `contracts/topics/topics.yaml` creates topics using `single` for one broker or `cluster` for three, matching installer behavior.
3. **Execution:** `task env:up [KAFKA=1|3]` starts the interactive environment with ports on `127.0.0.1`; `task env:test` runs smoke tests. Other tasks are `env:down`, `env:reset`, `env:ps`, `env:logs` and `env:endpoints`. A probe container checks HTTP readiness for shell-free Loki, Tempo, Collector and Toxiproxy images.
4. **Keycloak realm `psim`**, from `config/keycloak/psim-realm.json`: client `psim` owns `operator`, `supervisor`, `admin`, `integrator`, `auditor` and `manager` roles (ADR-020). `psim-console` is public and supports only Authorization Code + PKCE S256; `psim-services` uses service client credentials. SR-02 settings require passwords of at least 12 characters, brute-force lockout, 30 min session inactivity and a 12 h maximum. Custom login flow `psim-browser` requires a one-time password for roles including `mfa-required`, namely `admin` and `supervisor`. Test users are `<role>1`; ADR-061 defines `psim_scopes`.
5. **Credentials** are development-only plaintext values committed as `psim-dev-*`; this environment does not enable TLS. The release does not use them.
6. **Pipeline integration** (ADR-034): `task env:ci-test` creates project `psim-ci` from scratch without published ports, enforces the 300 s startup budget, creates topics, runs smoke tests and always removes the environment and data. The `env` stage runs in `task ci` when environment files, tools or the topic registry change, and always in `ci:nightly`. Adapter component tests (TS-03) are added to this stage.
7. **Smoke tests** in `tools/env/envctl.py` cover KRaft quorum; read/write through `INTERNAL` and Toxiproxy; registered topics; Protobuf schema registration; three PostgreSQL databases directly and through proxies; ReplicatedMergeTree through Keeper; direct and proxied Valkey; realm policy (SR-02); trace, log and metric delivery from Collector to stores; Grafana sources; and Toxiproxy delay effects.

## Consequences

- Cold startup including image downloads takes about 3.5 min; startup with images already present takes about 95 s for one or three brokers. Full `env:ci-test` takes about 2 min. Each environment uses about 4.2 GB RAM.
- Development and pipeline environments can run together, using about 8.5 GB.
- Use `task env:reset` when changing between one and three brokers.
- ADR-080 defines release log/trace stores, dashboards and alerts. The release generator configures TLS, mTLS, Kafka ACLs and store roles under ADR-043, ADR-014 and ADR-045.

## Verification

`task env:ci-test`, also with `KAFKA=3`, checks the startup budget and 16 smoke checks. A negative test starts one broker while expecting two: quorum and topic checks fail, and the environment is removed.
