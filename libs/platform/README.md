# Platform Libraries

<!-- language-links -->
[English](README.md) | [Русский](../../docs/ru/repository/libs/platform/README.md)
<!-- /language-links -->

General mechanisms for services according to ADR-001. The domain core uses its own ports and does not depend on these infrastructure libraries.

- `core`: Build information.
- `errors`, `log`, `config`, `observability`, `runtime`: Errors, structured logs, configuration, metrics, tracing, and service lifecycle.
- `async`: Shards, clocks, deadlines, and backpressure.
- `kafka`: Producer, consumer, transaction processor, serialization, and Schema Registry.
- [`pg`](pg/README.md): libpq, connection pool, transactions, migrations, unit-of-work template, inbox/outbox, and relays.
- `timers`: Durable timers in service schema, polling, and atomic operation with aggregate changes and outbox (ADR-012).
- [`redis`](redis/README.md): Boost.Redis, RESP3, Sentinel, pool, serialization, cache, limits, sessions, and notifications.
- [`security`](security/README.md): TLS/mTLS, JWT/JWKS, secrets, CSR, and revocation of active sessions.

- [`clickhouse`](clickhouse/README.md): Native TLS client, pool, limited requests, journaled Kafka batches, and migrations.

- [`s3`](s3/README.md): AWS SDK, SeaweedFS, limited file streams, SHA-256, conditional writes, and safe retries.

- `stream`: Shared stream runtime used by the [end-to-end flow](../../docs/engineering/event-flow.md), Kafka processor execution, serialization, and identifiers.

Platform core layers are implemented. [Simulation environment](../sim/README.md) is a separate library with manual tasks `sim:*`. Restrictions and unverified conditions are in [release readiness](../../docs/product/release-readiness.md).

- `connector`: [C++ SDK for connector](../../docs/engineering/connector-sdk.md), disk buffer, mTLS, stream management, commands, and state synchronization.

`identity` provides a read-only directory of Keycloak accounts: activity, roles, tenants, and access scopes. Incident Service, Response Engine, and Notification Service use one adapter to check recipients and responsible parties.
