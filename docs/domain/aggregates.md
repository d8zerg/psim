# Aggregates, Invariants, and Domain Events

<!-- language-links -->
[English](aggregates.md) | [Русский](../ru/domain/aggregates.md)
<!-- /language-links -->

For each context, aggregates, their invariants, commands, and domain events are described. An aggregate is a boundary of transaction consistency: invariants within an aggregate are always respected, and consistency between aggregates is eventually achieved through events.

State models are presented in [state-models.md](state-models.md), and sensor event types are in [event-taxonomy.md](event-taxonomy.md).

---

## 0. General Conventions

### 0.1 Identifiers

| Identifier | Construction | Who Creates | Why |
|---|---|---|---|
| `event_id` raw and normalized event | UUIDv7 | Connector SDK creates the event; stored in disk buffer | Re-sending after failure carries the same `event_id`, and deduplication is possible across the pipeline. The normalized event inherits `event_id` of the raw event: one-to-one mapping |
| `signal_id` | Named UUID from `(tenant_id, rule_id, rule_version, correlation_key, trigger_ref)`, where `trigger_ref` is `event_id` event that has completed, or boundary of absence rules window | Correlation Engine | Re-processing the same events after a failure still results in `signal_id`; Incident Service discards duplicates through the inbox |
| `incident_id` | UUIDv7 | Incident Service during creation | Uniqueness is ensured by the inbox based on `signal_id` and uniqueness of the open incident by grouping key |
| `run_id` | Named UUID from `(incident_id, plan_id, run_ordinal)` | Response Engine | Re-processing the event “incident created” does not trigger the scenario twice |
| `command_id` | Client’s idempotence key (UUIDv7) or named UUID from `(run_id, step_id, attempt)` for script commands | Client or Response Engine | Repeating the same request does not lead to double execution |
| `notification_id` | Named UUID from `(trigger_id, recipient_id, channel)` | Notification Service | Repeated triggering of the trigger does not send notifications twice |
| `audit_seq` | Monotonically increasing sequence number in tenant chain | Audit Service | Continuity of hash chains |
| `source_id` | Named UUID from `(connector_id, source_ref)` | Identically in Connector Gateway and Resource Catalog | The gateway calculates event key without a catalog, and the identifier does not change during source registration ([ADR-029](../adr/0029-deterministic-source-id.md)) |
| Others (locations, devices, rules, scenarios, users) | UUIDv7 | Service owner | - |

The algorithm for named UUIDs and namespace is defined in ADR-005.

### 0.2 Versions and Compatibility

- Each mutable aggregate has `version` (integer, +1 for each change). REST uses it as ETag; writes with outdated versions are rejected (`412`, code `COMMON_CONCURRENT_MODIFICATION`).
- Reference versioned entities (rules, scenarios) are immutable after publication: changes create a new version.

### 0.3 Domain Events

- Past-tense names: `IncidentCreated`, `CommandSent`.
- Each event carries `tenant_id`, the identifier and version of the aggregate after change, `actor` (user, service or connector), `occurred_at`, `causation_id` and `correlation_id` for causal tracing.
- Published in context topic through transactional outbox (for aggregates in PostgreSQL) or in Kafka transaction (for Correlation Engine).
- Each event reflecting an action of a subject creates an audit record.

---

## 1. Catalog

### 1.1 Aggregate `Location`

Site, building, floor or zone. Each location is a separate aggregate with a reference to the parent: this allows changing zones independently and not keeping the entire site in one transaction.

| Field | Description |
|---|---|
| `kind` | `site`, `building`, `floor`, `zone` |
| `parent_id` | Only empty for `site` |
| `site_id` | Denormalized identifier of the site for keys and ABAC |
| `name`, `code` | Code is unique among neighbors |
| `floor_plan` | Only for `floor`: reference to image, width and height in units |
| `timezone` | Only for `site`; used in reports and schedules |

**Invariants:**
- L1. Allowable parents: `building` -> `site`; `floor` -> `building`; `zone` -> `floor`, `building` or `site`.
- L2. `site_id` equals the identifier of the parent site chain.
- L3. A location cannot be deleted while it has child locations or attached devices. Deletion is logical (`archived`).
- L4. Moving a location to another site is prohibited - this changes the keys of partition history.

**Events:** `LocationCreated`, `LocationUpdated`, `LocationArchived`, `FloorPlanUploaded`.

### 1.2 Aggregate `DeviceType`

Reference: name, taxonomy category, source templates, list of capabilities with parameters and `critical` attribute.

**Invariants:** DT1. Capability code is unique within the type. DT2. The type cannot be deleted while there are devices of this type.

**Events:** `DeviceTypeDefined`, `DeviceTypeUpdated`.

### 1.3 Aggregate `Device`

Device with its sources: Sources do not exist without the device and change with it.

| Field | Description |
|---|---|
| `device_type_id`, `zone_id`, `site_id`, `connector_id` | Bindings |
| `external_id` | Device identifier in external system |
| `sources[]` | `source_id`, `external_ref`, source type |
| `position` | Coordinates on floor plan (optional) |
| `state` | `active`, `disabled`, `archived` |

**Invariants:**
- D1. `external_id` is unique within the connector - basis of idempotent import.
- D2. Source of `external_ref` is unique within the connector: connector uniquely addresses source.
- D3. `position` is allowed only if the device zone is on the floor with plan, and lies within the plan boundaries.
- D4. Device capabilities are determined by its type; command with code not in the list of capabilities is invalid.
- D5. Administratively disabled device (`disabled`) does not generate events for processing: Normalizer directs them to DLQ with reason `PROCESSING_DEVICE_DISABLED`.

**Events:** `DeviceRegistered`, `DeviceUpdated`, `DeviceMoved` (zone change), `DeviceDisabled`, `DeviceEnabled`, `DeviceArchived`.

### 1.4 Aggregate `Connector`

Registration includes name, type, protocol version, mTLS certificate fingerprint, limits, and state `registered`, `revoked`. Connector Gateway owns the persistent registration; Resource Catalog exposes and proxies its management.

**Invariants:**
- C1. At most one certificate fingerprint is active, except during rotation period (two fingerprints, the old one expires).
- C2. Revoked connection cannot open a session; opened sessions are closed.

**Events:** `ConnectorRegistered`, `ConnectorCredentialsRotated`, `ConnectorRevoked`, `ConnectorLimitsChanged`.

### 1.5 Import Operation

`ImportJob` is a process rather than a domain aggregate: a file, per-row results, and a summary. Each row is applied as a separate command to `Device`, with idempotency by `(connector_id, external_id)`.

### 1.6 The `MaintenanceWindow` Aggregate (ADR-042)

It represents a device or zone, its start and end times, reason, responsible party, suppression policy, and version. Commands: `ScheduleMaintenance`, `UpdateMaintenance`, `EndMaintenance`; Events: `MaintenanceScheduled`, `MaintenanceUpdated`, `MaintenanceEnded`. Invariants and temporal semantics are defined in [operational-capabilities.md](../architecture/operational-capabilities.md#4-maintenance-mode).

### 1.7 Floor Plan File Metadata (ADR-042)

The Resource Catalog stores `file_id`, resource owner, site, version, size, type, SHA-256, and object key. Binary content is read through port `FileStore`; replacements are recorded in PostgreSQL. Plan version and reference integrity are preserved in a backup copy.

## 2. Integration

### 2.1 The `ConnectorSession` Aggregate

Stored in Redis and only exists during the connection period.

| Field | Description |
|---|---|
| `session_id`, `connector_id` | - |
| `gateway_instance` | The gateway instance that maintains the connection; used for routing commands |
| `state` | See [state-models.md](state-models.md#4-connector-session) |
| `credits` | Available event flow credits |
| `last_heartbeat_at`, `last_acked_seq` by sources | - |

**Invariants:**
- CS1. A connector can have at most one active session. A new session replaces the old one (`SessionSuperseded`) so that reconnection after a break doesn’t wait for a timeout.
- CS2. The connector receives batch acknowledgement only after confirming the record in Kafka (`acks=all`).
- CS3. The gateway derives and validates `source_id` against the authenticated connector and source reference. Catalog mapping is checked by Normalizer; unknown sources go to its DLQ.
- CS4. The number of unconfirmed events does not exceed the granted credits.

**Events (operational, in metrics, and audit):** `SessionOpened`, `SessionSuperseded`, `SessionLost`, `SessionClosed`, `ConnectorRejected`.

### 2.2 Value object `RawEvent`

Immutable values: `event_id`, `connector_id`, `source_id`, `source_epoch`, `source_seq`, `occurred_at`, `received_at`, `raw_code`, `attributes` (string dictionary), `trace_context`.

**Rules:** RE1. `source_seq` strictly increases within the source epoch; the epoch increases when the device is reset. RE2. If `occurred_at` is later than `received_at` by more than the allowable time difference, the gateway uses `received_at` as the event time, retains the original value in the attribute, and generates `system.source.clock_skew` ([ADR-010](../adr/0010-time-semantics.md)).

---

## 3. Event Processing

### 3.1 The `MappingRuleSet` Aggregate

A versioned set of mapping rules for a connector type: `raw_code` and conditions on attributes -> taxonomy type, criticality, extracted attributes.

**Invariants:** M1. Each taxonomy type in the rules exists in the current taxonomy version. M2. For one `raw_code`, rules are ordered, and the first suitable one is executed. M3. The published version is immutable.

**Events:** `MappingRuleSetPublished`.

### 3.2 Value object `Event`

Normalized event: `event_id`, `type` (taxonomy code), `class`, `severity`, `site_id`, `zone_id`, `device_id`, `source_id`, `source_seq`, `occurred_at`, `received_at`, `normalized_at`, `attributes`. It remains unchanged after creation.

**Rules:**
- E1. Repetition with the same `(source_id, epoch, source_seq)` or `event_id` within the deduplication window is discarded.
- E2. An event without a device in the directory or without mapping rules goes to DLQ with a reason; it does not enter processing.
- E3. An event is significant if `severity >= tenant_threshold` (default `medium`) or referenced by a signal. Event History stores all normalized events; significant events have longer retention ([ADR-028](../adr/0028-clickhouse-analytical-storage.md)).

### 3.3 The `CorrelationRule` Aggregate

| Field | Description |
|---|---|
| `rule_id`, `version` | Each change is a new version |
| `state` | `draft`, `testing`, `active`, `archived` |
| `kind` | `single` (one event), `sequence`, `conjunction` (multiple sources in a window), `threshold` (N events per window), `absence` (expected event did not occur) |
| `correlation_key` | Declarative field: source, device, zone, floor, building or site |
| `window`, `allowed_lateness` | Window duration and allowable delay |
| `conditions` | Filters by taxonomy type, criticality, location, attributes |
| `suppression` | Suppression of repeated signals over time |
| `output` | Incident type, priority expression, text, grouping scope is no narrower than the correlation scope; the key includes incident type |

**Invariants:**
- R1. A rule is activated only after successful validation; an invalid rule cannot transition to `testing` or `active`.
- R2. The published version is immutable.
- R3. The window does not exceed the limits set by the configuration (default 24 hours) - capacity limit for state.
- R4. The grouping key refers only to fields guaranteed to be present in the signal.

**Events:** `CorrelationRuleDrafted`, `CorrelationRuleValidated`, `CorrelationRuleDryRunCompleted`, `CorrelationRuleArchived`.

### 3.4 The `RuleSet` Aggregate

A set of rules that apply to a tenant.

**Invariants:**
- RS1. A tenant has exactly one active version of the set.
- RS2. Activation is published in `psim.config.v1`. Each Correlation Engine instance applies a complete configuration snapshot between batches at its configuration-topic offset; a single event never mixes rule-set versions.
- RS3. The state of a rule whose version has not changed is preserved when switching sets; the state of the modified rule is reset.

**Events:** `RuleSetActivated`.

### 3.5 Value object `Signal`

`signal_id`, `origin` (rule or external source, [ADR-025](../adr/0025-ai-extension-points.md)), `rule_id`, `rule_version`, `correlation_key`, `grouping_key`, `incident_type_id`, `priority`, `site_id`, `zone_id`, `event_ids[]`, `first_event_at`, `last_event_at`, `emitted_at`, `summary`, `explanation_ref` (optional), `absence_deadline` (for absence of expected event).

A normal signal contains at least one event. Periodic checks on active resources in the directory can provide a signal without events; then `absence_deadline` is required. When grouping wider than the zone, the `zone_id` field is not filled to avoid assigning a zone to all signals from one participant.

**Rules:** SG1. `signal_id` is deterministic (section 0.1). SG2. The signal is published in a single Kafka transaction, with offset recording of the input topic and changelog writing - exactly-once.

---

## 4. Incident Management

### 4.1 Aggregate `IncidentType`

Code, name, default priority, grouping window, escalation policy reference, scenario necessity flag.

### 4.2 Aggregate `Incident`

| Field | Description |
|---|---|
| `incident_id`, `incident_type_id`, `site_id`, `zone_id` | - |
| `state` | See [state-models.md](state-models.md#1-incident) |
| `priority`, `priority_source` | Calculated or manually set (`computed`, `manual`) |
| `grouping_key`, `group_until` | Key and end time of the joining window |
| `signal_ids[]` | All joined signals |
| `assignee_id` | Responsible operator |
| `escalation_level` | 0 - no escalation; orthogonal to state |
| `blocking_steps[]` | Uncompleted mandatory steps of active scenario execution (from Response events) |
| `resolution`, `resolution_note` | Set when resolving |
| `timeline[]` | Chronology (stored in a separate table, but belongs to the aggregate) |
| `created_at`, `acknowledged_at`, `resolved_at`, `closed_at` | For MTTA and MTTR |

**Invariants:**
- I1. State transitions - only by state model.
- I2. A signal is joined to an incident no more than once (inbox according to `signal_id`).
- I3. A tenant has no more than one incident accepting new signals according to `grouping_key`. Expiration of the window releases this position, but does not complete the previous incident and its SLA. Ensured by a unique partial index (ADR-053).
- I4. A signal is joined to an open incident only if `occurred_at` of its first event ≤ `group_until`; otherwise, a new incident related to the previous one is created (`related_to`).
- I5. In states `acknowledged` and `in_progress`, a responsible person is assigned; the responsible person has role `operator` or `supervisor` and access scope including site and incident zone.
- I6. Calculated priority equals the maximum of signal priorities. Manual priority can only be set by `supervisor`; new signals increase it, but do not decrease it.
- I7. Resolution (`resolve`) is impossible until `blocking_steps` is empty. Exception - `resolution = duplicate` or `test` by a `supervisor` decision.
- I8. `closed` - final state. A signal by key of a closed incident creates a new incident.
- I9. `created_at ≤ acknowledged_at ≤ resolved_at ≤ closed_at` for given values.
- I10. Reopening (`reopen`) is possible only from `resolved`, cancels `resolution` and `resolved_at`, and is preserved in chronology.

**Commands:** `IngestSignal`, `Acknowledge`, `StartWork`, `Assign`, `Reassign`, `SetPriority`, `AddComment`, `AddLink`, `Resolve`, `Reopen`, `Close`, `ApplyEscalation`, `UpdateBlockingSteps`.

**Events:** `IncidentCreated`, `SignalAttached`, `IncidentPriorityChanged`, `IncidentAcknowledged`, `IncidentWorkStarted`, `IncidentAssigned`, `IncidentEscalated`, `IncidentCommentAdded`, `IncidentLinkAdded`, `IncidentResolved`, `IncidentReopened`, `IncidentClosed`.

**Closure:** `resolved` -> `closed` automatically at an adjustable interval (default 15 minutes) or manually `supervisor`. The interval allows reopening of incorrectly resolved incidents.

---

## 5. Response

### 5.1 Aggregate `ResponsePlan`

Versioned scenario: graph of steps and binding rules.

| Step Field | Description |
|---|---|
| `step_id`, `kind` | `instruction`, `checklist`, `decision`, `command`, `notify`, `wait`, `escalate`, `automated` |
| `mandatory` | Obligation |
| `next` | Next step; for `decision` - by step for each variant |
| `params` | Dependent on type: checklists items, decision options, command capability and target, recipients, waiting duration, call address |

Binding: type of incident, optional filters by sites, areas, and priorities; if multiple suitable scenarios, the most specific one is chosen, in case of equality - with higher binding priority.

**Invariants:**
- P1. The graph is acyclic, has exactly one initial step, all steps are reachable from the start.
- P2. For `decision`, the next step is defined for each variant.
- P3. Step `command` refers to a capability existing at least on one type of device; target is specified explicitly or with an expression ("all doors in the incident zone").
- P4. Published version is immutable; execution uses the version recorded at startup.
- P5. Bindings of different scenarios with the same specificity and priority do not overlap.

**Events:** `ResponsePlanPublished`, `ResponsePlanArchived`.

### 5.2 Aggregate `ResponseRun`

Scenario execution for an incident.

| Field | Description |
|---|---|
| `run_id`, `incident_id`, `plan_id`, `plan_version` | - |
| `state` | See [state-models.md](state-models.md#2-scenario-execution) |
| `steps{step_id -> state, actor, time, result}` | State of each step |
| `active_steps[]` | Steps available for execution |

**Invariants:**
- RR1. No more than one active execution per incident.
- RR2. A step is executed only if it is in `active_steps`.
- RR3. An obligatory step cannot be skipped (`skip`); an optional step can be skipped only by `supervisor` or the operator with a reason.
- RR4. A `command` step completes only when every command it issued reaches successful execution; an acknowledgement, failure, timeout or unknown outcome does not complete it.
- RR5. Every change to the set of incomplete required steps publishes an event for `Incident.blocking_steps`.
- RR6. Execution ends when all paths are completed or the incident is closed; when closing an incident, uncompleted steps move to `cancelled`.

**Events:** `ResponseRunStarted`, `StepActivated`, `StepCompleted`, `StepSkipped`, `StepFailed`, `MandatoryStepsChanged`, `ResponseRunCompleted`, `ResponseRunCancelled`.

### 5.3 Aggregate `Command`

| Field | Description |
|---|---|
| `command_id`, `device_id`, `connector_id`, `capability`, `params` | - |
| `incident_id`, `run_id`, `step_id` | Context (optional, command can be manual) |
| `requested_by`, `approved_by` | Subjects |
| `state`, `deadline_at`, `result` | See [state-models.md](state-models.md#3-command) |

**Invariants:**
- CM1. The capability is in the list of device types (D4).
- CM2. The initiator has permission to the command and access to the device area.
- CM3. A critical command is not sent until confirmation; the confirming one - `supervisor`, different from the initiator.
- CM4. Final states (`executed`, `failed`, `timed_out`, `rejected`, `cancelled`, `unknown`) are immutable; each command reaches the final state no later than `deadline_at` plus the timer expiration time.
- CM5. The result from the connector with an unknown or already final `command_id` is ignored and counted in metrics.

Events: `CommandRequested`, `CommandApprovalRequested`, `CommandApproved`, `CommandRejected`, `CommandSent`, `CommandAcknowledged`, `CommandExecuted`, `CommandFailed`, `CommandTimedOut`, `CommandCancelled`.

### 5.4 The `EscalationPolicy` aggregate

Acknowledgement and resolution SLAs by priority; escalation levels: delay, recipients (role within the scope, users), channels.

**Invariants:** EP1. Delays increase strictly with level. EP2. There is at least one recipient at each level.

Events: `EscalationPolicyChanged`.

### 5.5 The `SlaTracker` process

It’s not an aggregate with its own model, but a process on the timer service: when `IncidentCreated`, it sets the acknowledgement timer; when `IncidentAcknowledged`, it clears the timer and sets the resolution timer. The timer’s execution generates `ApplyEscalation` for the incident and notification level.

**Rules:** ST1. Timers are stored in PostgreSQL and survive restarting. ST2. Timer firing is idempotent by `(incident_id, sla_kind, level)`. ST3. The timer firing error is ≤ 1 second.

Events: `SlaBreached`, `EscalationLevelTriggered`.

### 5.6 The `Notification` aggregate

`notification_id`, recipient, channel, template, and data, status (`pending` -> `sent` -> `delivered` | `failed` | `unknown`), number of attempts.

**Invariants:** N1. It is idempotent with `notification_id`. N2. The number of attempts is limited by the channel policy; exhaustion results in `failed` and an audit record. N3. When the notification receiver’s threshold is exceeded, notifications are combined into a summary.

Events: `NotificationQueued`, `NotificationSent`, `NotificationDelivered`, `NotificationFailed`, `NotificationsAggregated`.

---

## 6. Identity & Access

### 6.1 The `Principal` model

For user requests, it is derived from the verified JWT and fresh Keycloak introspection: `user_id`, `tenant_id`, `roles[]`, `scopes[]` (for sites and access scopes). The source of role truth is Keycloak; access scopes are user attributes or Keycloak groups.

### 6.2 Access Decision

The function `decide(principal, action, resource) -> allow | deny(reason)`, where `resource` carries `site_id` and `zone_id`. It is a pure function in the general library, used by API Gateway and services.

**Invariants:**
- A1. Default prohibition: Actions without explicit permission are rejected.
- A2. The access scope for a site includes all its zones; the scope for a zone only includes that zone.
- A3. Each failure generates an audit record named `AccessDenied`.
- A4. The roles of `auditor` and `manager` do not have any modifying effects.

The “role × action” matrix and access scope checks are described in the [Authorization Guide](../security/authorization.md), based on [roles](../product/roles-and-journeys.md).

---

## 7. Audit

### 7.1 The `AuditChain` aggregate

One chain on the tenant.

| Field | Description |
|---|---|
| `audit_seq` | Number in the chain, without gaps |
| `source_event_id` | Identifier of the original domain event - idempotency key |
| `actor`, `action`, `object_type`, `object_id`, `site_id` | What happened |
| `occurred_at`, `recorded_at`, `outcome`, `details` | - |
| `prev_hash`, `hash` | `hash = SHA-256(prev_hash ‖ canonical_record_bytes)` |

**Invariants:**
- AU1. Only addition is allowed; modification and deletion are prohibited at the database rights level.
- AU2. `audit_seq` is continuous; `prev_hash` records are equal to `hash` from the previous record.
- AU3. Repeating a domain event with the same `source_event_id` does not create a new record.
- AU4. The checkpoint (`audit_seq`, `hash`, time) is periodically recorded and exported outside the database.

Events: `AuditCheckpointCreated`, `AuditIntegrityVerified`, `AuditIntegrityViolated`.

---

## 8. Reporting

Reporting owns projections, without independent business aggregates. Query semantics and access rules are described in the [reporting guide](../engineering/incident-reports.md). Definitions of metrics (MTTA, MTTR, false alarm rate) are provided in the [acceptance specifications](../product/acceptance-spec.md#s13-managers-report). A rebuild must produce the same result when the required source history is retained; otherwise a compatible snapshot or backup is required.

---

## 9. Coverage of acceptance scenarios

| Scenario | Aggregates and Processes |
|---|---|
| S1 | Connector, ConnectorSession, MappingRuleSet |
| S2 | Location, DeviceType, Device, ImportJob |
| S3 | RawEvent, Event, MappingRuleSet, Device |
| S4 | CorrelationRule, RuleSet, Signal |
| S5 | Signal, Incident, IncidentType |
| S6 | Incident, Principal (ABAC), feed projection |
| S7 | Incident, ResponsePlan, ResponseRun |
| S8 | Command, Device, ConnectorSession |
| S9 | EscalationPolicy, SlaTracker, Notification, Incident |
| S10 | AuditChain, access decision |
| S11 | Deterministic identifiers, inbox and outbox, ConnectorSession |
| S12 | Immutable versions of rules and scenarios, schema versions |
| S13 | Incidents (lifecycle times, resolutions), projections |

## 10. Operational Models (ADR-042)

- The `DeviceState` operational projection separately contains the connection, observed state, quality (actual/stale/unknown), source, epoch, sequence, event time, and last acknowledgment. The catalog state of the Device still describes its administrative lifecycle. Rechecking after reconnection does not replace the new state with an old snapshot.
- The responsibility transfer process is represented by `HandoverRequest`, which involves the recipient, a list of incidents and their expected versions, comments, acknowledgments, and line-by-line results. The command `AcceptHandover` applies changes to each incident atomically and publishes the previous event assignment with the reason for the transfer. SLA and ResponseRun are not restarted.
- `DutyAssignment` in the Response Engine connects the site to the current duty holder and backup recipient; changes are versioned and audited. The assignment history is retained; a calendar schedule is not part of the current model.
- `ReplayRun` stores replay_id, the initiator, the access scope, range, and the configuration version; any production mutations are prohibited by ACL. The results reside in an isolated environment.

Detailed invariants are defined in [operational-capabilities.md](../architecture/operational-capabilities.md). Versioned contracts and compatibility checks preserve these boundaries.

Idempotency prevents duplicate logical commands; it does not guarantee exactly-once physical execution. An unknown outcome is not retried automatically. The same restriction applies to notification channels with an indeterminate provider outcome.
