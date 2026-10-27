# ADR-058. API Gateway and Configuration Management for Normalization

<!-- language-links -->
[English](0058-api-gateway-and-configuration.md) | [Русский](../ru/adr/0058-api-gateway-and-configuration.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-020](0020-authorization-model.md), [ADR-026](0026-rest-contract-and-dto.md), [ADR-042](0042-operational-capabilities.md), [ADR-047](0047-event-flow.md), [ADR-052](0052-normalization-and-event-history.md)

## Context

The minimum Gateway only served the incident feed and WebSocket. REST handlers for other services are available within the test environment. Normalization mappings and retention settings were published by a trusted administrative tool. For the console, a single secure entry point, configuration versions, and data-quality snapshots are required.

## Solution

1. The API Gateway receives routes from Protobuf contract annotations. Each operation has a clear owner and actions from a common permission table. The Gateway checks the signature, issuer, audience, validity period, tenant, and role. The service owner rechecks the operation and the specific resource’s access scope. Trusted user headers do not replace the original Bearer token.
2. The external listener uses TLS. HTTP is allowed with separate settings only when open transports are allowed for the local test environment. Upstream addresses are set by process configuration; HTTPS checks the certificate chain and node name. Redirects are not performed. Requests after access and upstream checks are limited to 60 seconds; shutdown cancels unfinished operations. External application services ports, except for the Gateway and console, are not published.
3. Request rate is limited by token buckets based on connection address and tenant/user pair. According to [ADR-078](0078-shared-api-rate-limits.md), the user quota is shared among all replicas and shards and is stored in Valkey; connection limits are local. Exceeding limits return 429 and Retry-After; unavailable quotas return 503. CORS uses a list of origins; preflight is limited by existing routes. Responses receive nosniff, no-store, and security headers.
4. The Gateway checks idempotence keys and If-Match presence, sends them to the owner without modification, and saves the ETag of the response. Idempotence and optimistic concurrency are recorded in the owner’s transaction. Gateway errors use Problem Details; access failures are stored in the outbox. For unauthenticated failures, a zero technical tenant is used, without reading unverified token claims.
5. Request and response bodies are sent in batches of 64 KiB with backpressure. The floor plan limit is 20 MiB, a regular request is 1 MiB, and imports are 64 MiB. The file type and ETag are returned by Catalog; the Gateway allows PNG/JPEG, adds Content-Disposition, and nosniff. Image verification and interaction with object storage remain in Catalog.
6. Resource Catalog has APIs for normalization mappings, retention, and processing parameters. Normalizer and Event History remain consumers of `psim.config.v1`. Version changes, auditing, and outbox are performed in one PostgreSQL transaction. The initial version of an absent object is 0; creation requires `If-Match: "0"`. The next version is assigned by the server. Repeating keys with different content is rejected.
7. Before opening the API, Catalog restores previously published settings from the compacted topic up to a captured boundary. Then it updates its replica between operations. Switching from the administrative tool requires stopping its publication before accepting changes through REST. The shared domain condition evaluator is used both when publishing mappings and in Normalizer. Unknown taxonomy types and unsupported conditions are rejected before commit.
8. Temporary PostgreSQL projections store incident snapshots and operational resources. Snapshots and Kafka positions are read in REPEATABLE READ. REST is compatible with quality, confirmation time, and resume token. The catalog lifecycle, connection status, device availability, and observation freshness differ. History delivery does not update confirmation; an older snapshot cursor does not replace a new observation. When reading, freshness is recalculated by time and current lease session; registry errors do not create online status. Projections are transferred to Projection Service according to ADR-059; extending real-time subscriptions is defined in ADR-060.

## Consequences

- The Gateway does not store domain aggregates and does not repeat owner transactions.
- The overall request quota is not multiplied by the number of instances; local connection limits consider each instance’s capacity.
- Temporary snapshots are limited to 100,000 operational records and 10,000 incidents per tenant. The observation freshness threshold is 60 seconds. Indexes and individual source parameters develop together with full read models.
- Failures in IdP, upstream, and session registry are handled without expanding access rights.

## Verification

`task api:test` checks HTTPS, real JWTs, roles, access scope, CORS, ETag, version, idempotence, auditing, configuration publishing, snapshots, replay, restart, and transfer of an exactly 20 MiB floor plan while measuring Gateway memory consumption. Module tests check all contract routes, unknown paths, headers, limits, and configuration validation. `task api:sanitizers` runs ASan/UBSan and TSan checks.

## Transferring console models

[ADR-059](0059-console-projections.md) transfers models and Redis to Projection Service. The Gateway proxies snapshots via HTTPS and saves temporary real-time logs. The above-mentioned Gateway storage applies to the initial implementation.
