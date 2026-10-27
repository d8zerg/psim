# ADR-055. Device Command Service

<!-- language-links -->
[English](0055-command-service.md) | [Русский](../ru/adr/0055-command-service.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-008, ADR-017, ADR-018, ADR-020

## Context

The Command Service manages the lifecycle of commands and verifies the permissions of the initiator. The Connector Gateway delivers commands to the session, while the SDK prevents re-execution. After the session ends, the result of the action may be unknown.

## Solution

- The pure model `libs/domain/response` defines transitions and final states. PostgreSQL, using schema `command`, stores commands, query fingerprints, inbox, outbox, and local projections of Kafka resource and incident events. The service does not read schemas from other services.
- Authorization uses a common RBAC + ABAC library. The device type determines capabilities, parameters, and criticality; the client cannot disable acknowledgment. A critical command is confirmed by another user with the role of supervisor and access to the device and related incidents.
- The command identifier matches the Idempotency-Key. The same request from one initiator returns the current command; other content or initiators are rejected. State changes, audits, and response events are preserved in one transaction with the outbox.
- Before sending, the service reads the SessionRegistry from Valkey. A missing session results in `COMMAND_CONNECTOR_OFFLINE`; a reading error is not considered a lack of session. The delivery includes the identifier of the selected session, and the gateway accepts it only for that session. The gateway’s result also includes the session identifier.
- `SENT` indicates a delivery durably stored in the outbox; `ACKNOWLEDGED` requires a connector acknowledgement. A command can be canceled before sending. Acknowledgment is limited by a deadline. Losing the selected session after sending results in final state `UNKNOWN` with error `COMMAND_OUTCOME_UNKNOWN`; automatic transfer to a new session is prohibited. The result after the final state does not change it.
- The service publishes `CommandUpdated` in `psim.response.events.v1`. Incident Service stores these changes in the timeline without changing the lifecycle of the incident.
- Compatible Protobuf extensions add `UNKNOWN`, error codes, and session fields. Old deliveries without a session retain the previous behavior of the gateway; the new Command Service always indicates a session.

## Alternatives

- Direct reading of catalog and gateway tables violates data ownership.
- Automatic re-execution after reconnection does not allow distinguishing between lost responses and unexecuted commands.
- Storing commands only in memory does not ensure recovery of deadlines and results after process termination.

## Consequences

Local projections must receive resources before receiving commands. Valkey is needed before sending and when checking session changes. Each active command has a limited lifespan; if infrastructure is unavailable, processing resumes after restoration.
