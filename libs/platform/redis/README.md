# Valkey/Redis Layer

<!-- language-links -->
[English](README.md) | [Русский](../../../docs/ru/repository/libs/platform/redis/README.md)
<!-- /language-links -->

[ADR-015](../../../docs/adr/0015-redis-client-and-role.md). The CMake target is `psim::platform_redis`, and the namespace is `psim::platform::redis`.

Boost.Redis 1.90 operates with Asio without a second event loop and a blocking client. It uses an already compiled Boost package; the implementation of Boost.Redis is compiled once within the library. Standalone Valkey and primary replicas with Sentinel are supported. Routing of Redis Cluster, MOVED/ASK, and sharding are not implemented in MVP.

## Interfaces

| Title | Purpose |
|---|---|
| [connection.hpp](include/psim/platform/redis/connection.hpp) | RESP3, binary parameters, ordered pipeline, TLS, and primary detection |
| [pool.hpp](include/psim/platform/redis/pool.hpp) | Lazy pool with connection limits, FIFO waiting, and RAII leasing |
| [serde.hpp](include/psim/platform/redis/serde.hpp) | Versioned byte/Protobuf container and tenant-isolated keys |
| [patterns.hpp](include/psim/platform/redis/patterns.hpp) | Cache, atomic limits, TTL session registry, and notification publishing |

All errors are returned as `Result<T>` from the platform error catalog. Server diagnosis, keys, values, passwords, and certificate paths are not included in error messages. For server errors, only known categories are used, such as `WRONGTYPE` or `READONLY`.

## Connections and Sentinel

`ConnectionOptions` specifies the endpoint, credentials, TLS parameters, overall connection budget, and command deadline. TLS is enabled by default: the server chain and name are checked, TLS 1.2 and above are allowed. For plaintext development, it is specified explicitly through `tls.enabled = false`. A CA, client certificate/key, and server name can be set. Sentinel has separate credentials and TLS settings.

A non-empty `sentinel.endpoints` includes primary detection: the client contacts Sentinel in turn, receives the address through `get-master-addr-by-name`, connects, and checks `ROLE`. An outdated replica address is rejected; an unavailable Sentinel does not prevent trying the next one. Names returned by Sentinel must be allowed in the service network and present in node certificates.

Automatic reconnection of Boost.Redis is disabled. A timeout, cancellation, or network failure after sending a command may indicate an unknown write outcome. The library closes the connection and returns an error; the command is not automatically repeated. The pipeline maintains the order of results but is not a transaction: a server error in one command does not cancel others. Such errors are returned separately as `Reply::Kind::kError`.

The pool is created for each shard and used only by its executor. By default, 8 connections are available, with a 5-second wait for leasing, including checking the saved connection and connecting to a new one. Before providing a connection, `ROLE` is checked; after a break or role change, it is discarded, and the primary is re-detected through Sentinel. `close()` wakes up waiting ones; an already leased lease lasts until the operation is completed.

One operation is executed simultaneously on one connection; parallel operations get separate leases. Commands that change the state of connections such as AUTH/SELECT/MULTI/WATCH/SUBSCRIBE and blocking data wait commands are prohibited in this API. Applications must allow the ACL of the storage to execute commands; server administration is not the purpose of the pool.

Limitations: 1024 commands in the pipeline, 4 MiB for encoded requests, 4 MiB by default for response data, 8192 RESP3 nodes, and depth 32. The adapter checks limits before copying the next node; the network buffer is also limited. The maximum useful serialized payload is 1 MiB. For samples, the service additionally limits the number of results.

## Hot Data

`Keyspace(service, tenant)` requires non-empty segments and builds keys with each segment’s length. Binary data and separators within identifiers do not cause collisions. Different tenants and purposes `cache`, `limit`, `session`, `notification` are isolated. Each segment is limited to 512 bytes.

`Cache` returns a cache miss as an empty `optional`, and storage unavailability as an error. The record has a mandatory TTL from 1 ms to 30 days. `put/get` works with bytes, including empty and non-UTF-8 values. `put_encoded/get_encoded` accepts containers; `encode_protobuf` and `decode_protobuf<T>` check version, format, size, and full message name. Damage to the value and mismatch with the schema are not masked as cache misses.

`RateLimiter::consume` atomically executes a Lua script: the window starts with the first received request, TTL is set by the server, and the counter only increases up to the limit. The result includes the decision, number of accepted requests, and remaining time of the window. The limit is from 1 to 1 billion; TTL has the same limits. The application explicitly chooses behavior when unavailable: allowing degradation for non-critical limits or refusing for sensitive operations.

`TokenBucket::consume` maintains a shared burst capacity and continuously replenishes tokens at Valkey time. Keys `bucket` are separated from fixed windows `limit`. All clients of one key must set the same rate and burst; mismatch returns an error. TTL corresponds to the time of full replenishment, up to 30 days. After inactivity, the bucket expires; restarting a client does not create a new token balance. Backtracking time does not add tokens. Loss of data in the storage allows resetting the counter. API Gateway uses this mechanism for the user’s overall quota within the tenant and returns 503 when the storage fails.

`SessionRegistry` stores the gateway and connection generation token. `claim` uses NX + TTL, `renew` and `release` atomically compare the record before changing it. An old connection cannot extend or delete the record of a new owner. For each connection, the gateway creates a fresh and unpredictable token; the record fields must be correct UTF-8 and not exceed 512 bytes. The record serves as a hint for routing; checking the active session and rights is done by the gateway.

`Notifications::publish` sends the versioned container to the current tenant’s channel and returns the number of subscribers. The number of subscribers does not confirm message processing. The recipient uses RESP3 Pub/Sub and `decode`; subscription and its restoration belong to the service transport adapter. Pub/Sub does not store messages and does not guarantee delivery.

Valkey is not the source of truth. Replication is asynchronous, and recent changes may disappear upon switching. Services restore cache from PostgreSQL/Kafka, sessions are reconnected, and counters can be reset. Protection against the previous owner does not convert the registry into a distributed lock. For reliable events, Kafka and transactional outbox are used.

## Example

```cpp
redis::ConnectionOptions options;
options.endpoint = {"valkey.example", "6379"};
options.credentials.password = secret;
options.tls.ca_file = "/etc/psim/trust/ca.pem";
redis::Pool pool({.connection = options});
redis::Cache cache(&pool, {"resource-catalog", tenant_id});
auto saved = co_await cache.put(resource_id, projection, std::chrono::seconds{30});
if (!saved) {
  co_return std::unexpected(saved.error());
}
auto cached = co_await cache.get(resource_id);
```

For separate monitoring connections, `require_primary=false` allows reading replicas and Sentinels without checking the writability. Work client pools retain the primary check.

For Sentinels, `options.sentinel.endpoints`, `primary_name`, credentials, and TLS are filled in. The static endpoint is used only without Sentinels. Configuration and secret retrieval remain with the service adapter.

## Verification

```text
task env:up
task test:redis
task test:redis:sanitizers
task test:component
```

Tasks run from an isolated profile `valkey-ha` with a primary, two replicas, three Sentinels, and a separate mTLS-endpoint. Each run starts with a clean test topology. The usual local `valkey` and its data remain separate. In HA scenario, the current primary stops responding, Sentinels automatically raises the replica, and the pool resumes writes under load. Also verified are Toxiproxy breaks, absence of automatic write retries, memory limits, TTL, atomicity of limits, session tokens, Protobuf, and notifications. Module coverage is checked together with module tests, with a threshold of 75%.

TLS and trust verification are performed with component tests; operational ACLs, certificate rotation, placement on different VMs, and system scenario S11 are covered by [trust procedures](../../../docs/security/platform-trust.md) and [deployment qualification](../../../docs/engineering/deployment-topologies.md).
