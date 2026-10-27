# Deployment

<!-- language-links -->
[English](README.md) | [Русский](../docs/ru/repository/deploy/README.md)
<!-- /language-links -->

| Directory | Contents | Decision |
|---|---|---|
| `local/` | Local development and integration testing environment (Docker Compose) | [ADR-035](../docs/adr/0035-local-environment.md) |
| `images/` | Service container images (Debian 13, non-root) | [ADR-034](../docs/adr/0034-local-ci-and-supply-chain.md) |
| `keys/` | Public release verification key | Distributed with the release; trust is established independently |

Systemd units, the installer and monitoring packages are described in [deployment topologies](../docs/engineering/deployment-topologies.md).

## Local Environment

The stack runs on a Linux host with native Docker Engine and Docker Compose. The current user needs access to the local Docker daemon.

```
task env:up              # Start (KAFKA=3 uses three brokers; change broker count through env:reset)
task env:endpoints       # Development addresses and credentials
task env:test            # Smoke tests against the running environment
task env:ps              # Container status; env:logs SERVICE=<name> shows logs
task env:down            # Stop and retain data; env:reset deletes data
task env:ci-test         # Fresh pipeline environment: no ports, 5-minute budget, smoke tests, cleanup
```

- `local/compose.yaml` - All components; `local/compose.ports.yaml` - Ports for the interactive environment, only on `127.0.0.1`.
- `local/config/` - Configurations: PostgreSQL (database and roles), ClickHouse and Keeper, realm Keycloak `psim`, OpenTelemetry Collector, Prometheus, Loki, Tempo, Grafana sources, proxy Toxiproxy.
- Services and tests in the Compose network refer to components by name (`kafka-1:19092`, `postgres:5432`, `clickhouse:9000`, `valkey:6379`, `registry:8080`, `keycloak:8080`, `otel-collector:4317`); for network failures, it uses `toxiproxy` (Kafka `toxiproxy:29092`, PostgreSQL `:15432`, Valkey `:16379`, ClickHouse `:19000`).
- Kafka topics are created from the [`contracts/topics/topics.yaml`](../contracts/topics/topics.yaml) registry; automatic topic creation is disabled.
- Database accounts `psim-dev-*` - Only for development.

## Valkey HA Stack

`task env:valkey:ha` runs the `valkey-ha` profile: one separate primary, two replicas, three Sentinels with a quorum of 2 and mTLS-endpoint `valkey-tls:6380`. Each run recreates only this test profile with a clean topology; certificates are generated in the ignored `.cache/valkey-tls`. It is automatically used by `task test:redis`, `task test:redis:sanitizers`, and `task test:component`. The profile does not publish ports and is isolated from the regular Valkey. The Sentinels use the password `psim-dev-sentinel`; the storage is `psim-dev-valkey`. `env:down` also stops the HA containers; `env:reset` deletes their state.

Tests use `PSIM_TEST_REDIS`, `PSIM_TEST_REDIS_FAULT`, and the list `PSIM_TEST_SENTINELS` from `cpp:_run_env`. In the production cluster, deployment and TLS are defined by [ADR-015](../docs/adr/0015-redis-client-and-role.md) and the [deployment guide](../docs/engineering/deployment-topologies.md).

## Security Stack

`task env:security` runs the `security` profile: isolated `kafka-tls:19092`, `postgres-tls:5432`, and `valkey-tls:6380`, with client certificate verification. It recreates only test nodes, does not publish ports, generates certificates in `.cache/security-tls` and `.cache/valkey-tls`. `task test:security`, `task test:security:sanitizers`, and `task test:component` use this profile; Keycloak receives `psim` in the audience access token of the console and services. `PSIM_TEST_KEYCLOAK` refers to the isolated development backend, and the library client JWKS is verified via HTTPS.

The regular Compose retains protocols and database accounts. Clients explicitly choose exceptions; delivery uses TLS and separate service permissions according to [ADR-043](../docs/adr/0043-platform-security.md). The test PKI is not used during platform installation; the operator’s external PKI is described in [trust protocol](../docs/security/platform-trust.md).

The MVP file storage compatible with S3 is SeaweedFS; clients and integration requirements are described in [S3 layer](../libs/platform/s3/README.md). The local bucket is private, and roles for installation and applications are separated. See [deployment topologies](../docs/engineering/deployment-topologies.md) for HA configuration and automated delivery, and [administration and recovery](../docs/engineering/administration-and-recovery.md) for backups.

The end-to-end event flow runs through `task flow:up`; processes and command checks are described in [guide](../docs/engineering/event-flow.md).
