# ADR-047. Minimal end-to-end event flow

<!-- language-links -->
[English](0047-event-flow.md) | [Русский](../ru/adr/0047-event-flow.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-001, ADR-004-ADR-008, ADR-016-ADR-020, ADR-029, ADR-039-ADR-043

## Context

Component checks for platform adapters are supplemented by end-to-end checks for contracts, process boundaries, and browsers.

## Solution

1. Run five separate C++ services: Connector Gateway, Normalizer, Correlation Engine, Incident Service, and API Gateway. The `connectors/simulator` generator uses the actual gRPC protocol for connectors; the model test environment `libs/sim` remains a separate manual tool.
2. Use existing topics: raw -> normalized -> signals -> incident events, Protobuf, and Confluent wire format with Schema Registry identifiers. The gateway confirms events after Kafka acks=all. Normalizer and Correlation Engine record outputs and offsets in one Kafka transaction. Services do not call each other directly for event processing.
3. Use one shard per process for the first integration. `libs/platform/stream` contains execution and serialization infrastructure; application handlers remain in their own services. gRPC callback reactors transfer work to shards; gRPC callbacks are not blocked. The gateway and simulator use EventEngine at the process level: after RPC resources are freed, they terminate gRPC, wait for working threads, and release OpenSSL. PKI temporary files and secrets are created during test environment setup; keys are read through `security::read_secret`.
4. Limit demonstration data to one tenant, registered connector, source `door-1`, site, and access scope. `ALARM` provides `intrusion.zone.alarm` and a rule signal; `RESTORE` is normalized without a signal. Unknown sources or code are sent to DLQ. Source and signal identifiers are calculated using UUIDv5 with fixed namespace from `contracts/identifiers`; new entities receive UUIDv7.
5. Incident Service records inbox, aggregate, and outbox in one PostgreSQL transaction. Repeating the same signal does not change the aggregate. The grouping window is 60 seconds from the first event; serialization by grouping key uses an advisory transaction lock. A separate outbox relay publishes saved bytes with confirmation to Kafka; Kafka offset is fixed after database transaction. Outbox delivery must be at-least-once.
6. The initial integration had no separate Projection Service; API Gateway maintained a minimal PostgreSQL projection. It is isolated by instance name. Each API process start creates a separate temporary Kafka group and reads all partitions; it does not wait for session timeout of the old process after SIGKILL. Projection and inbox in the database prevent repeated initial reads. Snapshots and offset vectors are read in one Repeatable Read transaction. WebSocket uses contract v1, with a limit of 256 changes and limited client queues. The token contains offset vector, tenant, and instance epoch; after restart or out of journal, a new snapshot is required. This historical mechanism has been replaced by the general Projection Service and realtime channel via ADR-059 and ADR-060.
7. Connector uses TLS 1.3 and a certificate from a known URI SAN. REST and the first WebSocket frame check JWT Keycloak: issuer, audience, signature, expiration, tenant, and role as operator/supervisor. Users enter through Authorization Code + PKCE. Access/refresh tokens remain in the page memory; sessionStorage only stores temporary PKCE parameters. The test realm has a fixed tenant; technical credentials do not allow access to the feed.
8. Local Kafka, Registry, PostgreSQL, Keycloak, and OTLP use explicit plaintext exceptions within the test environment; external ports are loopback-bound. Connector certificates are checked here too. Secure delivery is described in the [test environment guide](../engineering/deployment-topologies.md). Applications run as a non-root user, with a read-only root filesystem and no capabilities; the test profile directory is mounted separately.
9. Enable browser scenario in the CI environment phase. It checks double sending, WebSocket recovery without additional snapshots, SIGKILL API with subsequent synchronization, continued updates, and Kafka/database consistency. Tempo must contain one causal trace for all six components with existing parent spans.

## Boundaries

This is a limited integration test environment. Product implementations are defined by ADR-048 (Connector SDK), ADR-049 (Connector Gateway), ADR-051 (catalog), ADR-052 (normalization), ADR-053 (incidents), ADR-059 (projection), ADR-061 (authorization), and ADR-071 (console). Event time in fixtures is limited to five minutes, packets to one event and 64 KiB, active gRPC sessions to eight, API connections to 32, snapshots to 1,000 incidents. Cluster resilience is not claimed by this test environment.

## Testing

Scenarios and commands are described in the [test environment guide](../engineering/event-flow.md). The base line measures time from `occurred_at` to `produced_at` of a signal read committed, and to the commit line of React through `useLayoutEffect`. The generator follows an open schedule; delays over 10 ms make the base line invalid. Measurement on a single host uses the same kernel clock. The small integration run is not a load test for the full SLO profile.

## Source

[gRPC C++ callback lifecycle](https://grpc.io/docs/languages/cpp/best_practices/): OnDone is called after Finish and after reactions are completed; the server reactor is freed only in OnDone.

Laboratory gateway restrictions have been replaced with [ADR-049](0049-connector-gateway.md): continuous registration, protocol 1.1, history sending, packets up to 1000 events, and server session control. Projections and realtime have been replaced by ADR-059 and ADR-060 solutions; laboratory test environment restrictions do not automatically apply to secure delivery.
