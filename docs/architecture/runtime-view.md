# Runtime View

<!-- language-links -->
[English](runtime-view.md) | [Русский](../ru/architecture/runtime-view.md)
<!-- /language-links -->

Section 6 arc42. Key interaction scenarios between components. The numbers of stages B1-B9 are from [the delay budget](quality.md#4-delay-budget).

---

## R1. Event -> Incident in the console (S3-S6)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Connector (SDK)" as SDK
    participant "Connector Gateway" as GW
    participant "Kafka" as K
    participant "Normalizer" as N
    participant "Correlation Engine" as CE
    participant "Incident Service" as IS
    participant "PostgreSQL" as DB
    participant "API Gateway (realtime)" as API
    participant "Console" as UI

    SDK->>SDK: Event: event_id, source_seq -> disk buffer
    SDK->>GW: EventBatch (Credits)
    GW->>K: produce psim.ingest.raw.v1 (acks=all)
    K-->>GW: ack
    GW-->>SDK: BatchAck -> remove from buffer
    K->>N: raw (partition by source_id)
    N->>N: Deduplication, mapping, enrichment from a catalog replica
    N->>K: txn: normalized + state + offsets
    K->>CE: Normalized (partition by site:zone)
    CE->>CE: Rules, RocksDB state
    CE->>K: txn: signal + changelog + offsets
    K->>IS: signal (partition by grouping_key)
    IS->>DB: txn: inbox + incident + outbox
    IS->>K: outbox relay -> IncidentCreated
    K->>API: IncidentCreated (all partitions)
    API->>API: ABAC filter by site/zone
    API->>UI: WebSocket delta
    UI->>UI: Rendering, sound
@enduml
```

## R2. Incident Acceptance and Response Scenario (S7)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Console" as UI
    participant "API Gateway" as API
    participant "Incident Service" as IS
    participant "Kafka" as K
    participant "Response Engine" as RE

    note over RE: By IncidentCreated: select a scenario, ResponseRunStarted,\nMandatoryStepsChanged -> Incident.blocking_steps
    UI->>API: POST /v1/incidents/{id}:acknowledge (If-Match: version)
    API->>API: JWT, ABAC
    API->>IS: acknowledge
    IS->>IS: Invariants I1, I5, outbox IncidentAcknowledged
    IS-->>UI: 200 + new version
    UI->>API: POST /v1/runs/{run}/steps/{step}:complete
    API->>RE: complete step
    RE->>RE: RR2, graph transition, outbox StepCompleted and StepActivated
    K-->>UI: Deltas through realtime (Kafka -> API Gateway -> Console)
    UI->>API: POST /v1/incidents/{id}:resolve
    API->>IS: resolve
    IS-->>UI: 409 INCIDENT_MANDATORY_STEPS_PENDING (if blocking_steps is not empty)
@enduml
```

`blocking_steps` is updated in the Incident Service based on Response events. To prevent resolving an incident while the event activating a mandatory step is still in transit, the Response Engine publishes `MandatoryStepsChanged` in the same transaction as the activation of that step. When `resolve`, the Incident Service additionally checks whether the response-run events have been applied to a version known to the client (which the client provides in its request).

## R3. Device Command (S8)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Console" as UI
    participant "Command Service" as CS
    participant "Redis (sessions)" as R
    participant "Kafka" as K
    participant "Connector Gateway (all instances)" as GW
    participant "Connector" as SDK

    UI->>CS: POST /v1/commands (Idempotency-Key = command_id)
    CS->>CS: CM1, CM2, critical -> awaiting_approval
    CS->>R: Is there a connector session?
    alt No session
        CS-->>UI: failed COMMAND_CONNECTOR_OFFLINE
    else There is
        CS->>K: outbox -> psim.commands.v1 (key connector_id), state sent
        K->>GW: Command
        GW->>SDK: Command (only instance with a session)
        SDK->>SDK: Execute no more than once per command_id
        SDK-->>GW: CommandAck, then CommandResult
        GW->>K: psim.commands.results.v1
        K->>CS: Results -> Acknowledged / Executed / Failed
        CS->>K: outbox -> psim.response.events.v1
    end
    note over CS: deadline_at timer -> timed_out
@enduml
```

## R4. SLA escalation (S9)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Kafka" as K
    participant "Response Engine (timers)" as RE
    participant "Incident Service" as IS
    participant "Notification Service" as NS

    K->>RE: IncidentCreated (P2)
    RE->>RE: Set the timer ack = created_at + 60 seconds (PostgreSQL).
    note over RE: Timer polling every 200 ms, locking the PostgreSQL aggregate
    RE->>RE: The timer has been triggered; the incident has not been acknowledged.
    RE->>K: outbox SlaBreached, EscalationLevelTriggered(1)
    K->>IS: ApplyEscalation -> escalation_level = 1, IncidentEscalated
    K->>NS: Notify recipients at level 1
    NS->>NS: notification_id is deterministic, with in-app and email channels
    NS->>K: NotificationSent / Delivered -> Notification
@enduml
```

## R5. Reconnecting the connector after a failure (S11)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Connector (SDK)" as SDK
    participant "Gateway-1" as GW1
    participant "Gateway-2" as GW2
    participant "Redis" as R
    participant "Kafka" as K

    SDK->>GW1: EventBatch #41 (seq 1000-1099)
    GW1->>K: produce
    note over GW1: GW1 fails before BatchAck
    SDK->>SDK: Connection lost, events are being stored in the buffer
    SDK->>GW2: connect (mTLS) + Register(last_acked: 999)
    GW2->>R: Connection session -> GW2 (remove the old one)
    SDK->>GW2: EventBatch seq 1000-…
    GW2->>K: Produce (seq 1000-1099 - duplicates)
    note over K: The Normalizer discards seq 1000-1099 based on the tracker.
@enduml
```

## R6. Activation of a set of rules (S4)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Administration" as UI
    participant "Rule Management (Correlation Engine)" as RM
    participant "Kafka" as K
    participant "Instances of the Correlation Engine" as CE

    UI->>RM: POST /v1/rules (Draft)
    RM->>RM: Validation (R1, R3, R4)
    UI->>RM: POST /v1/rules/{id}:dry-run {from, to}
    RM-->>UI: Signals that the rule would generate
    UI->>RM: POST /v1/rule-sets:activate
    RM->>K: psim.config.v1 ruleset/<tenant> (Version N+1)
    K->>CE: All instances read the change.
    CE->>CE: Between batches: compilation, saving unchanged rules, switching
@enduml
```

## R7. Failure of the Correlation Engine instance (S11, QS-A1)

1. With `group.protocol=consumer`, the broker removes an unresponsive member after `group.consumer.session.timeout.ms` (45 s by default, 10 s in the local profile). The classic fallback uses `session.timeout.ms`.
2. The consumer protocol incrementally reassigns partitions; the classic fallback uses cooperative rebalancing.
3. The new owner initializes the transaction producer with the same `transactional.id` - unfinished transactions from the previous owner are interrupted by the broker.
4. The partition state is restored from `psim.correlation.changelog.v1` to the local RocksDB state. The ≤ 30 s recovery target requires qualification under the declared load and failure model.
5. Reading continues with the committed offset; signals published in interrupted transactions are invisible to `read_committed` consumers and will be republished with the same `signal_id`.

## Audit and Independent Verification

Producers publish actions through the outbox in `psim.audit.v1`; the Audit Service reads the available history from the beginning of retention. Keycloak events are imported by a read-only client with a persistent cursor. Two instances serialize the addition by a PostgreSQL lock for the tenant. The record contains a continuous number, previous hash, and the deterministic Protobuf hash.

The periodic checkpoint is signed with Ed25519 and synchronously recorded in an external archive. Then, a copy of it and the Kafka outbox are stored via a database transaction. The verification process recomputes record hashes and compares the archive independently of the checkpoint tables. The export records its own chain head and contains the entire chain; `psim-audit-tool` verifies it without using the database or network, using separately authorized keys. [Guide](../engineering/audit-service.md) describes the permissions, protection limits, and recovery procedures.

The diagrams describe protocol boundaries. A physical device action is not guaranteed exactly once; an unknown command outcome must be reconciled before a new command is issued. See [command handling](../engineering/command-service.md) and [release readiness](../product/release-readiness.md).
