# ADR-049. Connector Gateway: Registration, Sessions, and Revocation Confirmation

<!-- language-links -->
[English](0049-connector-gateway.md) | [Русский](../ru/adr/0049-connector-gateway.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-006, ADR-008, ADR-015, ADR-017, ADR-018, ADR-042, ADR-043, ADR-048

## Context

The initial laboratory gateway accepts one test connector. For multiple instances, durable registration, unique session ownership, server-controlled heartbeat, command delivery, and revocation without retaining access through an open stream are required. The Resource Catalog is introduced separately; trust registration should not depend on the temporary configuration of the test environment.

## Solution

1. The gateway stores registrations, versions, and revoked certificates in the `ingest` schema in PostgreSQL. Access is only granted to active registrations of its tenant: URI SAN `psim://connector/<connector_id>`, SHA-256 certificate fingerprint, and certificate validity period must match. Public certificates are verified against the configured trusted bundle; private keys are not transmitted. For rotation, two valid certificates can be simultaneously accepted. A revoked certificate is not added again.
2. The `ConnectorAdministrationService` service uses the same mTLS listener but only accepts URI `psim://service/gateway-admin` and verified JWT with role `admin` or `integrator`. The tenant is determined from the JWT. Recovery requires `admin`; the administrator’s MFA policy is provided by the IdP. The operation checks `expected_version`; the registration, result, and audit-outbox records are executed in one transaction. Repeating with the same operator, tenant, and `request_id` returns the saved result; another payload is rejected.
3. A global registration revision is applied by each gateway. Trust cache is valid for no more than one second measured by the monotonic clock. Updates close inappropriate streams; the gateway acknowledges the revision after removing these transports. The management response waits for confirmation from all active instances. If the gateway stops updating its three-second lease, it must stop working with expired cache. The management deadline does not cancel a recorded transaction: the client repeats the original request with the same identifier.
4. The canonical session lease is stored in PostgreSQL. Acquisition is done by locking the registration row and does not replace the active owner; the certificate is rechecked in the same transaction. Extension does not revive the expired lease. Redis contains an additional TTL lease `(tenant_id, connector_id)` with a random session token and gateway startup identifier. Acquisition uses `NX`; extension and deletion compare the previous token. A new instance does not replace the existing lease. After a crash, connection may wait up to 15 seconds. The gateway stops working before the conservatively calculated local lease time; an ambiguous extension result closes the stream. Loss of a Redis record does not create a second owner: PostgreSQL rejects competing acquisitions. Redis unavailability blocks reception and does not invalidate the durability of the client journal.
5. Negotiate protocol `1.1` or `1.0` and state, snapshot and command capabilities. The batch is limited to 1000 events, 1 MB, registration settings, and issued credits. Redis atomically limits the number of events per second. All batch records are sent simultaneously, but `BatchAck` is only issued after individual confirmation of each accepted Kafka record with `acks=all`. Final errors for individual events are listed in the response. Uncertain delivery is not confirmed; repetition maintains the identity of the event.
6. Heartbeat is expected every 5 seconds; absence of three intervals closes the stream. Independent timers are not blocked by Kafka waiting. The TTL update considers the last client heartbeat time. Connection status and session expiration are recorded in PostgreSQL with outbox; another gateway publishes `offline` for expired sessions after a session owner failure.
7. Topic `psim.ingest.state.v1` contains authenticated connection changes and equipment reports. A new session indicates `unknown` for equipment quality. The snapshot is agreed separately; the old cursor inside the session does not replace the new observation. Raw events retain the authenticated receiving session and metadata of the current observation. History, foreign sessions, and old times cannot establish freshness. Application normalization is described in ADR-052, REST/WebSocket state projections in ADR-059 and ADR-060.
8. Commands are read according to ADR-018: a separate ephemeral group for gateway process run, all partitions, starting from end. Assignment is received before opening the listener. The gateway checks the tenant, connector, canonical PostgreSQL lease, execution capability, and deadline, delivering only to its active session and receiving responses only for commands previously delivered to it. The list of unfinished commands is limited; unknown results are processed by the Command Service. The SDK log excludes repeated execution within its durability guarantee.
9. Transport uses gRPC callback API; all mutable session state are processed on a single Asio shard. Reading, writing, and waiting in Kafka are independent. The queue of outgoing messages is limited. `OnDone` releases the reactor after completing transport reactions; shared state is retained until asynchronous lease release is completed. Stopping drains operations until infrastructure clients are destroyed.

## Consequences

- Trust registration is available until the Resource Catalog appears through a separate CLI; transferring business management to the catalog does not change the session protocol.
- Raw event delivery remains ALO+ID. A crash between Kafka and client response allows repetition; consumer deduplication is required.
- Redis entry is a recoverable hint for the Command Service; ownership and validity periods are additionally checked locally before delivery.
- Logs of management requests and revoked certificates require backup with PostgreSQL. Their cleanup should not allow reuse of revoked identities.
- Connection state data does not prove the presence of a fresh device state. Consumers consider the session, source cursor, and observation time.

## Verification

Unit tests of protocol boundaries and trust; component verification with actual mTLS, Kafka, PostgreSQL, Redis, and IdP; rotation and revocation through another instance; heartbeat without client responses; history and snapshot after a new event; SIGKILL for the gateway with automatic SDK recovery and identity comparison; measurement of two instances with confirmed Kafka records.
