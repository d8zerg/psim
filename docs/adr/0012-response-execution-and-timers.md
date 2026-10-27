# ADR-012. Response-plan execution and timers

<!-- language-links -->
[English](0012-response-execution-and-timers.md) | [Русский](../ru/adr/0012-response-execution-and-timers.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-008, ADR-018, ADR-020, ADR-025, ADR-042, ADR-055

## Context

A response plan contains instructions, checklists, decisions, commands, notifications, waits, escalations and external handlers. Execution preserves the selected version and resumes after process termination. Timer error in an operational environment must not exceed one second. Publishing an action and changing state must commit atomically.

## Decision

1. The pure `libs/domain/response` interpreter validates graph acyclicity and reachability and manages steps, attempts and mandatory actions. One step is active on each path. Branching selects one successor; mandatory steps on unselected branches no longer block the incident.
2. Response Engine PostgreSQL stores plan versions, run snapshots, resource projections, idempotency keys, external-handler requests, inbox and outbox in schema `response`. The plan snapshot is copied into the run. Archiving neither changes its definition nor stops existing runs. Each incident gets one run with a deterministic identifier.
3. Shared library `libs/platform/timers` provides persistent timers in the owner's schema. Instances poll deadlines every 200 ms. Before modifying a timer, lock its aggregate row. A conditional timer update records firing in the same transaction as the domain action and outbox. Retrying after termination does not create a second effect.
4. PostgreSQL aggregate locking determines timer ownership. Ownership is not tied solely to a Kafka partition owner: APIs and automatic transitions create timers independently of current assignments. Another instance can therefore resume as soon as the lock is released. Command Service retains its existing deadline mechanism without data migration.
5. `CommandRequested` in `psim.response.events.v1` is the durable Command Service port. Publication is restricted by service identity, and the handler verifies the initiating service. Command identifiers are deterministic from run, step, attempt and device. Command Service rechecks its local catalog, device and incident scope, parameters and criticality. Manual requests retain the authenticated user as initiator; automatic requests use Response Engine. A different supervisor approves a critical command; a manual-step initiator cannot approve their own command. Only terminal statuses complete the step. A new attempt creates a new identifier.
6. `NotificationRequested` records notification intent. The step completes once intent is written to outbox; Notification Service handles channel delivery. Escalation publishes the existing `EscalationLevelTriggered` event.
7. An automatic external step permits only an HTTPS address in the service configuration's exact allowlist, without redirects, with TLS verification, a bounded response and a 1-30 second deadline. Sending is recorded beforehand. An unknown outcome after termination or timeout fails the step without automatic resend. Explicit Retry creates a new attempt. The handler receives `effectId` for its own idempotency.
8. The separate `psim-response-replay` program uses the interpreter and simulated adapters. It reads the plan and actions from stdin, writes results to stdout, and creates no runtime or Kafka, PostgreSQL or HTTP clients. Verification runs it without networking, credentials or sockets. Commands, notifications, escalations and handlers produce diagnostic records only.

## Alternatives

- Temporal or Camunda adds a separate subsystem and execution model excessive for the MVP's bounded graphs.
- Delayed Kafka messages require custom deadline emulation.
- Redis is not a source of truth and cannot atomically change a PostgreSQL aggregate and outbox.
- Tying timers to Kafka partitions adds coordination for APIs and multiple input streams.

## Consequences

Locks serialize changes within one run; different runs execute independently. Overdue timers are processed after PostgreSQL recovers, so the accuracy objective applies to an operational environment. Resource projections must be populated before a plan is published. Device commands are restricted to the incident's zone. Supported targeting uses an explicit device UUID or `zone.devices`, limited to 128 targets. Other expression syntax is rejected during validation.

A missing matching plan is recorded in audit, a counter and PlanUnavailable. Incident Service adds a warning for types requiring a response. Complete operational Kafka ACL and deployment network-policy configuration is covered by [deployment topologies](../architecture/deployment.md).

## Verification

Checks cover graphs and transitions, publication and binding conflicts, fixed run versions, roles and scopes, separate critical-command approval, SDK results, errors and Retry/Override, termination of both instances during a wait, cancellation on incident closure, timer accuracy and zero external effects from diagnostic replay. Commands and artifacts are described in the [guide](../engineering/response-engine.md).
