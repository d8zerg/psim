# API Gateway

<!-- language-links -->
[English](api-gateway.md) | [Русский](../ru/engineering/api-gateway.md)
<!-- /language-links -->

The API Gateway provides REST v1 and serves WebSocket `/v1/realtime`. Routes are formed from HTTP annotations in Protobuf. Resource owners check access scopes and record changes.

## Configuration

`settings.tls_certificate_file` and `settings.tls_private_key_file` set up the external TLS listener. `listen_plaintext` is only allowed with `allow_plaintext: true` for local tests. `allowed_origins` contains exact allowed origins; wildcards are not used.

For each owner, `<service>_host` and `<service>_port` are set: `catalog`, `rules`, `incidents`, `commands`, `response`, `history`, `reports`, `projection`, `audit`, `notifications`. `upstream_ca_file` defines the trusted chain; if needed, `upstream_certificate_file` and `upstream_key_file` are used. JWT is verified through `issuer`, `jwks_url`, and `jwks_ca_file`.

`rate_per_second` and `rate_burst` set the overall user quota within each tenant for all API Gateway replicas and shards. The Valkey atomic token bucket uses server time and continuous replenishment; the same user in different tenants gets independent keys. Rebooting the Gateway does not reset quotas. Exceeding the quota returns 429 with `Retry-After`; a storage error returns 503 before the request is forwarded to the API owner. The Gateway does not fall back to local quotas when Valkey fails. Counters are temporary and can be reset if Valkey data is lost or its asynchronous replicas are switched.

`ip_rate_per_second` and `ip_rate_burst` limit incoming connections from a single transport address locally. Connection limits, memory, and WebSocket control frames also remain local.

For the overall quota, `redis_host`, `redis_port`, `redis_user`, `redis_password_file`, `redis_ca_file`, `redis_certificate_file`, and `redis_key_file` are set. TLS is required without explicit `allow_plaintext`. `redis_sentinels`, `redis_primary_name`, `redis_sentinel_user`, and `redis_sentinel_password_file` include detection of the primary through Sentinel. The pool is limited; obtaining a connection, connecting, and issuing commands have budgets of 500 ms each. All replicas must use the same rate and burst: discrepancies with the existing bucket are rejected. Keys are removed after enough time for full replenishment.

10 seconds are allocated for TLS handshake and client headers; after access verification, requests and upstream have a limit of 60 seconds. Stopping the Gateway cancels active operations, including waiting for upstream.

Snapshots are served by [Projection Service](console-projections.md). The Gateway sends requests and original JWT via HTTPS. Cache of snapshots and checking the current connector session belong to Projection Service; API Gateway uses its Valkey account for overall request quotas.

## Processing Configuration

`GET /v1/settings` returns retention and processing parameters. `PUT /v1/settings/retention`, `PUT /v1/settings/processing`, and `PUT /v1/mappings/{connectorType}` require If-Match with the current version. For new objects, `"0"` is used. An idempotency key can also be supplied for PUT.

Changing retention applies to newly saved events; previously prepared blocks’ durations are not recalculated.

Publishing is managed by Resource Catalog. `taxonomy_path` settings specify the taxonomy file to be provided. First, publishers from the previous administrative tool must be stopped; Catalog will load its latest versions when running. After switching, changes are executed via REST.

## Compatibility and Updates

Contracts maintain `/v1`, existing Protobuf field numbers, and existing WebSocket frame format. Quality, confirmation time, connection status, and snapshot token fields are added compatibly; old clients continue to receive previous realtime delta types. The new error `COMMON_PAYLOAD_TOO_LARGE` returns HTTP 413.

Before switching external traffic, configure the Gateway certificate, trust in API owner certificates, and allowed console origins. Tokens must contain tenant and access scopes; absence of an access scope does not grant access to resources. Owners’ internal REST listeners are retained for Gateway.

Before updating, prepare Valkey and Sentinel accounts for API Gateway, permissions for its bucket keys and connection secrets. The profile generator includes them in the delivery. Previous rate and burst values now apply to all replicas; there’s no need to multiply the limit by the number of instances. Overall quota is ensured after switching all replicas to the new implementation. Solution - [ADR-078](../adr/0078-shared-api-rate-limits.md).

Migrating Catalog adds a configuration table; temporary snapshots use a separate PostgreSQL schema. Existing events and tables are not deleted. After starting configuration via REST, the previous administrative tool cannot be resumed simultaneously with Catalog.

## Observability

Metrics `/metrics` include number and duration of REST requests per contract operation, active requests, access failures, rate limits, and transmission errors. `psim_api_rate_limit_failures_total` separately counts failures due to storage unavailability; Grafana dashboard and Prometheus alerts are added for this. Arbitrary paths and user or object identifiers are not used as labels. HTTP spans are sent upstream via W3C trace context. Local Prometheus collects Gateway metrics; Grafana includes an API Gateway dashboard and alerts for recurring transmission errors.

Memory limits are checked during normal build. ASan retains freed buffers to detect memory errors, so its RSS is evaluated separately from the delivered build.

## Testing

```sh
task api:test
task api:sanitizers
task api:coverage
```

Testing creates a separate Compose environment with real Keycloak, Kafka, PostgreSQL, Valkey, Catalog, and S3. Results are recorded in `build/resource-catalog/<preset>/api-result.json`; Gateway logs are in the adjacent `api-servers.log`.

Overall quota is tested on two replicas with two runtime shards each. Quota burst, retention of counters after restarting one replica, user independence, ACL from delivery generator, HTTP 429, HTTP 503 when Valkey stops, and recovery after it returns are checked. Additional replica logs - `api-limit-servers.log`. Component tests for Valkey additionally check tenant isolation, replenishment, TTL, and inconsistent settings.

Architectural boundaries and time limits are described in [ADR-058](../adr/0058-api-gateway-and-configuration.md).

## Realtime

Subscriptions, filters, client capabilities, Kafka resumption, current state images and `realtime_*` parameters are described in the [realtime guide](realtime.md). The gateway reads both domain topics completely, and current state images are obtained from the Projection Service using the original JWT.
