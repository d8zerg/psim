# ADR-078. Shared quotas for REST API in Valkey

<!-- language-links -->
[English](0078-shared-api-rate-limits.md) | [Русский](../ru/adr/0078-shared-api-rate-limits.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-015, ADR-023, ADR-058, ADR-059

## Context

The local token bucket API Gateway provided separate quotas for each instance. When replicas were added, the total user quota increased. For temporary shared counters, Valkey is already used; command data, idempotency and writer ownership remain in PostgreSQL.

## Solution

1. The REST request quota is set by a pair of tenant/user and is shared across all replicas and runtime shards. No separate shared limit for an entire tenant is introduced with this solution.
2. Keep the semantics of `rate_per_second` and `rate_burst`: token bucket with continuous replenishment and acceptable burst. A fixed window does not replace this algorithm.
3. Perform checks, replenishment, token consumption and TTL updates using a single Lua script in Valkey. Time is provided by the server; time of different Gateway processes is not included in calculations. The key is removed after enough time for full replenishment. Inconsistent parameters of one key are rejected.
4. Exceeding the quota returns HTTP 429 with `Retry-After`. Storage error returns HTTP 503 before the request is sent to the API owner. Automatic retry of writes with unknown outcomes and local quota replacement are not performed.
5. Limits on incoming connections based on transport IP, memory, and WebSocket control frames remain local. Authorization is checked before the shared quota; its results are not transferred to the shared cache.
6. Use a separate API Gateway account, TLS, and permissions only for its own bucket keys. In the cluster profile, primary is defined through Sentinel. Sentinel password and storage password are independent.

## Consequences

Rebooting the Gateway preserves the quota, scaling does not multiply the burst. All replicas use the same settings. Before updating, ACLs and secrets are prepared; the shared quota is ensured after all replicas switch to the new implementation.

Valkey becomes a mandatory dependency for authorized REST requests. Quota failures are considered separately with a metric, panel, and alert. Projection caches maintain their own policy for returning to PostgreSQL.

Counters are temporary. Loss of Valkey data, key expiration, or asynchronous replica switching can restore the initial burst. The quota does not replace object limits and durable idempotency of operations in PostgreSQL.

## Testing

`task api:test` and `task api:sanitizers` test two replicas with two runtime shards, total burst, quota preservation during Gateway reboot, user independence, generated ACLs, HTTP 429, HTTP 503, and recovery after Valkey stops.

`task test:redis` and `task test:redis:sanitizers` additionally test concurrent clients, tenant isolation, continuous replenishment, TTL, inconsistent configuration, and backwards clock movement.

Configuration and compatibility - [API Gateway](../engineering/api-gateway.md). Server semantics - [Lua scripting](https://valkey.io/topics/eval-intro/) and [TIME](https://valkey.io/commands/time/).
