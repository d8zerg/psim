# Incident Service

<!-- language-links -->
[English](incident-service.md) | [Русский](../ru/engineering/incident-service.md)
<!-- /language-links -->

The service manages the lifecycle of incidents, grouping and responsibility; decisions are [ADR-020](../adr/0020-authorization-model.md), [ADR-027](../adr/0027-incident-states.md) and [ADR-053](../adr/0053-incident-service.md).

## Launch and Configuration

The `psim-platform/psim-incident-service` package and image use a common runtime. Kafka, Schema Registry, and PostgreSQL are required for processing; REST works via HTTPS with JWT verification. An example of local configuration is [incident-service.yaml](../../deploy/local/config/event-flow/incident-service.yaml). Parameters `tls_certificate_file`, `tls_private_key_file`, `tls_ca_file`, and `jwks_ca_file` set trust levels. Plaintext connections are allowed only with the explicit `allow_plaintext` in an isolated environment.

For recipient verification, `directory_base`, `directory_client`, `directory_roles_client` (UUID of the roles client `psim` in Keycloak) and `directory_secret_file` with permission 0400 from the service user are specified. The base URL points to Keycloak with realm `psim`; the client has `realm-management/view-users` and `view-clients` without modification rights. The user contains attributes `psim_tenant` (one value) and `psim_scopes` (list of access scopes), which are consistent with the issued JWT. Requests do not accept roles or recipient access scopes from the browser. If no configuration is set or Keycloak fails, assignment and handover are rejected; signal processing remains ongoing.

## REST and Concurrency

Public paths include `/v1/incidents`, incident cards, timeline, signals, commands like acknowledge, start-work, assign, set-priority, resolve, reopen, and close, collections for comments and links; types are `/v1/incident-types`. JSON corresponds to published Protobuf and OpenAPI. POST requires a UUID Idempotency-Key; modifying an existing aggregate is done with If-Match with a quoted version. Repeating with the same content returns the previous result; version conflict returns 412. Fields not in the contract are rejected.

The `/v1/incidents:transfer` batch can handle up to 100 items. Each item contains incidentId, expectedVersion, recipientId, and a comment; transferId and expectedVersion are used for confirmation, and the JWT belongs to the recipient. The response results maintains order and includes incident or errorCode for each row. The batch is not a single transaction. Until confirmation, the previous operator handles it; each action is saved in the timeline and audit.

Viewing and searching are filtered by tenant, site, and zone. An administrator without an additional role of operator/supervisor does not process incidents. Whether the recipient is present in the console is not checked. Supervisors may assign incidents; operators may assign only to themselves; transferring to another operator requires confirmation.

## Durability

Signals, transitions, timeline, audit, and outbox use PostgreSQL transactions. Kafka offsets are committed after the database commit; outbox is published separately. Re-publishing events is possible; their identifier and version allow the consumer to avoid duplicate effects. A complete list of signals is available on a per-page basis; preview in incidents is limited to 1000 elements, with a counter and a flag indicating truncation.

Events `RunUpdated` from `psim.response.events.v1` update required steps and run version; outdated snapshots are ignored. Escalation level changes separately from the state.

Changing the type does not affect already created incidents. Automatic closure survives restarts and is performed by one of the instances. Expiring a grouping window does not close the previous incident or remove its responsibility.

## Verifications

```sh
task incidents:test
task incidents:bench
task incidents:sanitizers
task incidents:coverage
task incidents:sim
```

Profiles and reports are saved in `build/incidents/<preset>/`. Signal messages are processed one by one: waiting for previous transactions in the already fetched batch is included in measured latency. The metric `psim_incident_signal_seconds` measures the duration of signal processing until the PostgreSQL transaction is completed; waiting time for Kafka and outbox publication are measured separately. A short local measurement does not confirm long cluster SLOs.
