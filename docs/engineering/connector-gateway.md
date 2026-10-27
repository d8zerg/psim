# Connector Gateway

<!-- language-links -->
[English](connector-gateway.md) | [Русский](../ru/engineering/connector-gateway.md)
<!-- /language-links -->

[ADR-049](../adr/0049-connector-gateway.md) · [Command Routing](../adr/0018-command-routing.md)

The gateway accepts gRPC/mTLS sessions from connectors, checks persistent registration, records events in Kafka, and delivers commands. The source code is `services/connector-gateway`; the administrative client is `tools/gateway-admin`.

## Launch and Verifications

```bash
task env:up
task gateway:test
task gateway:failover
task gateway:bench
task gateway:sanitizers
task gateway:coverage
```

Verifications use two real processes, synthetic certificates, and the IdP registration of the local environment. `gateway:bench` uses a release build, 16 connectors, and subsequent Kafka verification; message generation occurs before the measurable interval. The sampling frequency for traces in this set is 0.001; heartbeat, registry, and Kafka acknowledgments remain enabled. `gateway:failover` stops the active gateway with SIGKILL and switches the test TCP proxy to a second instance. The SDK saves incoming events in its log and reconnects without changing the address, identifiers, or sequences. For TSan, a separate Compose override allows the system call `personality`, required by the runtime sanitizer on hosts with high entropy ASLR; normal startup preserves Docker seccomp profile. Diagnostics are saved in `build/gateway-test/<preset>`.

In a regular test environment, `task flow:up` registers the demo connector through an authenticated administrative service. Its certificate is no longer embedded in the gateway code. The certificate generator and bootstrap IdP are only intended for isolated local environments.

## Configuration

- `settings.listen_port` - gRPC port, default 8080; the administrative service and connector stream use different identity checks.
- `settings.tls_directory` - `ca.pem`, `server.pem`, and protected `server.key`. External transport requires TLS 1.3 and client certificates. The trust bundle must contain the chain required to verify connector public certificates.
- `settings.brokers`, `settings.registry` - Kafka and Schema Registry. For Kafka, set `kafka_ca_file`, `kafka_certificate_file`, `kafka_private_key_file`; disabling TLS is only possible explicitly with `allow_plaintext` for local environments.
- `settings.pg_host`, protected by `pg_password_file`, `pg_ca_file`, `pg_certificate_file`, `pg_private_key_file` - PostgreSQL with name verification and CA. The schema `ingest` is created by a versioned migrator.
- `settings.redis_host`, protected by `redis_password_file`, `redis_ca_file`, `redis_certificate_file`, `redis_key_file` - Redis/Valkey. Redis errors stop acknowledgment and session ownership.
- `settings.issuer`, `settings.jwks_url` - Fixed parameters of the IdP, audience `psim`, client roles `psim`. Tenants are taken from verified `psim_tenant`.
- `settings.security_audit_tenant` - Centralized audit tenant for unregistered connectors. For known registration, it uses its saved tenant. This field is required when TLS is active; TLS failures before establishing a verified identity remain in the transport boundary log.
- `settings.max_sessions` - Limit of simultaneous transports per instance, default 256.

Service passwords and secret keys are read from protected files. Paths and trusted endpoints are set by the operator; connector messages and JWT do not select JWKS addresses or other infrastructure.

## Registration Management

The CLI requires a mTLS certificate `admin.pem`, with protected private key `admin.key` with URI `psim://service/gateway-admin`, trusted `ca.pem`, and the operator’s protected JWT file:

```bash
psim-gateway-admin connector-gateway:8080 /secure/gateway-admin request.json /secure/operator.token
```

`request.json` contains `requestId`, `action`, `connector`, `expectedVersion`, and for adding a certificate, `certificatePem`. During initial registration, `connector` contains `connectorId`, `name`, `connectorType`, and optional `limits`. The service calculates the certificate fingerprint and validity period. The connector certificate requires URI `psim://connector/<connectorId>`.

| Operation | Action |
|---|---|
| `REGISTRATION_ACTION_INSPECT` | Read your tenant’s registration and its version |
| `REGISTRATION_ACTION_REGISTER` | Create a new registration; `expectedVersion` is 0 |
| `REGISTRATION_ACTION_ROTATE` | Add a new certificate with the previous one retained; no more than two active ones at once |
| `REGISTRATION_ACTION_REVOKE` | Revoke `fingerprint`, close affected streams, and prohibit subsequent connections |
| `REGISTRATION_ACTION_DISABLE` | Disconnect registration and close its streams |
| `REGISTRATION_ACTION_RECOVER` | Only `admin`: revoke previous certificates and activate a new, unused one |

Roles `admin` and `integrator` are limited to their tenant. The administrator’s MFA is configured in the IdP. The CLI does not create its own CA and does not accept connector secret keys.

Every change checks the current version of the registration. For repetition after a network error, the same `requestId` and payload are used. The response deadline may mean that the change is already committed, and confirmation of application is still pending. Using a new identifier instead of retrying does not resolve this uncertainty.

A successful response to the change means that live instances have applied the revision. The trust lease lasts for one second; an instance with an unavailable registry stops processing. The service lease expires after three seconds. Audit and registration changes are recorded in one transaction, and the audit publication is repeated through the outbox.

## Session Semantics

- `Register` comes first. `1.1` or `1.0` is selected; new messages are only allowed if they match the corresponding capabilities.
- The canonical lease of the connector is stored in PostgreSQL; the Redis record serves as an additional constraint and routing hint. Losing this record does not allow a second session. The lease lasts no longer than 15 seconds. After the old gateway fails, reconnection waits for it to expire; if it completes correctly, the lease expires earlier.
- Heartbeat occurs every 5 seconds. Events and server heartbeat do not replace client heartbeat. Three missed intervals close the stream and publish `offline`.
- The batch is limited by credits, 1000 events, 1 MiB, and registration settings. Repeated or decreasing `batch_seq` within the stream is rejected.
- `BatchAck.accepted` includes only records confirmed by all in-sync Kafka replicas. Final errors are listed separately; no ACK is given for uncertain delivery.
- Raw events retain their original time and identity when delivered. `live_observation` requires the current session and recent observation; history remains history.
- Commands are delivered only to the active session of the same tenant, with an unexpired deadline and announced support for commands. The receipt and result are published in `psim.commands.results.v1`. Responding to a command not sent by this session or dealing with an overloaded queue does not trigger execution.

## Status and Operation

`psim.ingest.state.v1` has the key `connector_id`. The record contains a tenant, session identifier, and either a changed connection or a verified `StateReport`. `Envelope.sequence` increases monotonically in PostgreSQL for each connector, allowing for the rejection of out-of-order notifications in the outbox. Connection publishes quality `unknown`; this does not make devices available. An offline notification with quality `stale` disables any previously confirmed freshness; a source without verified observation remains `unknown`. The old snapshot cursor within a session is filtered if a new live observation has arrived. Raw events retain `gateway_session_id`, `live_observation`, and `observation_session_id` for subsequent normalization and projection.

`/health/live`, `/health/ready`, `/metrics`, and the trace are provided by the runtime. The gateway exports the number of open sessions, accepted and ultimately rejected events, delivered commands, heartbeat delays, and connection failures. Metrics do not use connector identifiers as labels.

Resource Catalog, Normalizer, and the REST/WebSocket state projection are described in their service guides. The Command Service is responsible for action rights, confirmation of critical commands, overall deadlines, and unknown results. The Gateway does not execute commands independently.
