# ADR-015. Redis-compatible store, client and responsibilities

<!-- language-links -->
[English](0015-redis-client-and-role.md) | [Русский](../ru/adr/0015-redis-client-and-role.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-016, ADR-018, [deployment.md](../architecture/deployment.md#3-cluster-on-vms-topology)

## Context

Redis stores connector sessions, rate limits and hot projections such as the feed. Redis uses RSALv2/SSPL and, from version 8, also AGPLv3. These licenses are undesirable for distribution within a commercial product under the requirement to use open licenses that permit distribution.

## Options considered

### Server

| Option | Advantages | Disadvantages |
|---|---|---|
| Redis 7.4+ / 8 | Original implementation | RSALv2/SSPL/AGPL impose distribution constraints |
| **Valkey** (Redis 7.2 fork, BSD-3, Linux Foundation) | Open license, protocol compatibility, active development | - |
| KeyDB, Dragonfly | Performance | Dragonfly uses BSL; limited KeyDB development |

### Topology

| Option | Advantages | Disadvantages |
|---|---|---|
| Redis Cluster (sharding) | Scales storage capacity | MVP data occupies megabytes; clients need slot routing and redirects; multi-key operations are restricted |
| **Primary + 2 replicas managed by Sentinel** | Sufficient MVP capacity; simple client; automatic failover | No horizontal write scaling, which is not required |

### Client

| Option | Advantages | Disadvantages |
|---|---|---|
| **Boost.Redis 1.90** | Native Asio, coroutines, RESP3, pipelining; Boost is already pinned | The platform layer implements primary discovery through Sentinel; no Cluster routing |
| redis-plus-plus (hiredis) | Cluster and Sentinel | Its asynchronous libuv mode uses a different event loop |

## Decision

1. Use **Valkey**: a primary and 2 replicas managed by Sentinel in the cluster, with 3 Sentinel instances on infrastructure nodes; a single instance on one machine. HA uses Sentinel; Redis Cluster is outside the supported configuration. Sharding and MOVED/ASK handling are outside MVP scope; adopting Cluster requires a separate decision and an adapter behind the same service ports.
2. Use Boost.Redis 1.90 behind [`libs/platform/redis`](../../libs/platform/redis/README.md): RESP3, asynchronous DNS, TLS with CA and hostname validation, bounded requests and responses, and pipelining. The platform layer implements `SENTINEL get-master-addr-by-name` discovery and `ROLE` validation according to the [Sentinel client specification](https://valkey.io/topics/sentinel-clients/). Sentinel and data-store credentials and TLS settings are independent.
3. **Responsibilities:** Redis is not a source of truth. All its data is recoverable: connector reconnection restores the session registry; Kafka and PostgreSQL restore projections; rate-limit counters may reset. Losing Redis degrades functionality without losing durable data.

## Consequences

- [deployment.md](../architecture/deployment.md) and acceptance criteria use the Sentinel topology.
- Services are tested with Redis unavailable: functionality degrades while event processing continues.
- A bounded lazy pool belongs to one shard. Before reusing a connection, it checks `ROLE`; after connection loss it discovers the primary again. A sent command with an unknown outcome must not be retried: timeout, cancellation or disconnection returns an error and closes the transport. The service determines operation recovery policy.
- Valkey replication is asynchronous: failover may lose recent hot-data changes. Rate-limit counters may reset, caches require a TTL, and connector reconnection restores sessions. Compare-and-renew/delete with a generation token protects a new entry from an old owner on the current primary; it is neither a distributed lock nor an authorization mechanism.
- `PUBLISH` notifications use a versioned envelope but provide neither delivery guarantees nor history ([Pub/Sub semantics](https://valkey.io/topics/pubsub/)). Reliable events use Kafka and the outbox.
- The `valkey-ha` test profile is isolated from the regular local Valkey. It contains a primary, two replicas, three Sentinels and a separate mTLS endpoint. Production quorum, ACL, TLS and node placement are described in the [topology guide](../engineering/deployment-topologies.md).

## Verification

`task test:redis` checks automatic failover when the primary stops responding under load, Sentinel rediscovery, Toxiproxy network interruption, deadlines, memory limits, atomic limits, TTL, tenant isolation, session protection and notification delivery. `task test:redis:sanitizers` runs component scenarios under ASan/UBSan and TSan; `task test:component` checks module coverage of at least 75%. System chaos test S11 and replica placement on separate VMs require subsequent qualification.
