# Device Command Service

<!-- language-links -->
[English](command-service.md) | [Русский](../ru/engineering/command-service.md)
<!-- /language-links -->

The Command Service provides the HTTPS API `/v1/commands`, stores the lifecycle in its own PostgreSQL schema `command`, and delivers actions through the Connector Gateway and SDK. Solution: [ADR-055](../adr/0055-command-service.md).

## Requests and Access

`POST /v1/commands` accepts `commandId`, `deviceId`, `capability`, `parameters`, as well as an optional link to an incident and scenario step. The header `Idempotency-Key` equals `commandId`. The server determines criticality based on device type, checks capabilities, parameters, and device and connector activity. Up to 64 parameters, up to 128 bytes for the name, and up to 1024 bytes for the value are allowed. The Protobuf message size is limited to 30,000 bytes to ensure that command fields with routing fields fit within the 32 KiB SDK limit. The same request from the same initiator returns the current state; other content or initiators result in `COMMON_IDEMPOTENCY_KEY_REUSED`.

Reading is available to operators, supervisors, and auditors. Requests and cancellations are available to operators and supervisors; approvals and rejections are restricted to supervisors. The general authorization library checks tenant and device scopes and related incidents. Approving one’s own request is prohibited, including by supervisors. Rejections are recorded in audits.

`GET /v1/commands` supports filters `incidentId`, `deviceId`, `states`, `pageSize`, and `pageToken`. The page size is no more than 1000; the cursor continues searching by identifiers. The list contains only available commands. Changes to `:approve`, `:reject`, and `:cancel` require `If-Match` with an ETag version and UUID in `Idempotency-Key`. A repeated committed request with the same key returns the current command, even if the version has changed.

The Response Engine sends step commands through CommandRequested in `psim.response.events.v1`. The recipient checks the service sender and initiator identity. Manual steps keep the verified user as the initiator; automatic ones are handled by the Response Engine. The delegated scope is limited to the incident zone; catalog, criticality, and second-user approval checks are performed by the same processor as for the HTTPS API. A final rejection upon receiving a request is recorded as FAILED and published in the response. Repeating the same identifier does not create a second execution.

## Delivery and Result

- Non-critical commands are sent immediately. Critical ones remain in `AWAITING_APPROVAL`; before delivery after approval, the device configuration is rechecked.
- SessionRegistry Valkey selects a session. Missing a session terminates the command with `COMMAND_CONNECTOR_OFFLINE`; a Valkey error is returned as an infrastructure error.
- Delivery in `psim.commands.v1` contains `session_id`. The gateway sends it only to the selected session. Republishing outbox is safe due to the durable SDK log.
- `SENT` indicates a saved delivery in outbox. A receipt from `psim.commands.results.v1` converts the command to `ACKNOWLEDGED`, and the result to `EXECUTED` or `FAILED`. The result is checked against tenant, connector, and session; inbox excludes duplicate effects.
- Losing a session after sending terminates the command in `UNKNOWN` with `COMMAND_OUTCOME_UNKNOWN`. Automatic sending to a new session is prohibited. The operator checks the device status before performing another action.
- Deadlines terminate commands awaiting approval with `COMMAND_APPROVAL_TIMED_OUT`, sent ones with `COMMAND_TIMED_OUT`. Late results do not change the final state. After restarting, saved deadlines are checked.

Changes to commands, audits, and outbox are recorded in one transaction. `CommandUpdated` is published in `psim.response.events.v1`; Incident Service adds each version of the command to the timeline, including delayed changes, while maintaining the lifecycle, responsible person, and SLA of the incident.

## Configuration and Operation

Common parameters for streaming services on PostgreSQL, Kafka, Schema Registry, JWT/JWKS, and TLS are used. Additional parameters: `redis_host`, `redis_password_file`, `redis_ca_file`, `redis_certificate_file`, `redis_key_file`, `command_timeout_seconds` (default 30, range 1-240). HTTPS uses `tls_certificate_file` and `tls_private_key_file` or `command-server.pem` and `command-server.key` in `tls_directory`.

Catalog data and incident snapshots are read from Kafka into their own monotonic PostgreSQL projections. Requests are rejected before resources arrive. To restore projections, saved records from the original topics are needed. The service does not read tables from other services. Changes to the device access scope after creating a command require a new request; an old pending command can be canceled.

Runtime diagnostics: `/health/live`, `/health/ready`, `/metrics`, OTLP, and JSON logs. The metric `psim_command_result_seconds` measures the transaction of result processing; `psim_command_results_ignored_total` counts repeated, unknown, and already completed results. Background processing errors are visible in logs; saved outboxes and deadlines are processed again after dependency restoration.

## Testing

All commands run on Linux with native Docker Engine and Docker Compose. Tests use a separate Compose project `psim-command-test`; the project name is set in [testing tasks](../../tools/taskfiles/commands.yml):

```
task commands:test
task commands:sanitizers
task commands:coverage
task commands:bench
```

The test runs two Command Services, Incident Service, the actual gateway, SDK, Kafka, PostgreSQL, Valkey, and Keycloak. Artifacts are located in `build/commands/<preset>/`: reports, process logs, SDK executions, and input sets. The release measurement sends 10 commands per second according to MVP command volume ([ADR-018](../adr/0018-command-routing.md)), checks identifiers of all commands, and verifies p99 from the start of HTTPS requests to the invocation of the SDK handler within 200 ms.
