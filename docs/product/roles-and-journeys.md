# Roles and User Journeys

<!-- language-links -->
[English](roles-and-journeys.md) | [Русский](../ru/product/roles-and-journeys.md)
<!-- /language-links -->

Roles are used as the basis for RBAC. The access scope (site, zone) restricts roles through ABAC: for example, an operator can only see incidents related to their own sites.

These journeys describe required behavior. Qualification evidence and outstanding checks are listed in [release readiness](release-readiness.md).

---

## 1. Roles

| Code | Role | Purpose | Permissions | Scenarios |
|---|---|---|---|---|
| `operator` | Operator | Processing incidents in real time | Viewing the feed and incident cards within the assigned scope; accepting, tracking, and resolving incidents; executing scenario steps; sending non-critical commands; commenting | S6, S7, S8 |
| `supervisor` | Shift Supervisor | Monitoring of shifts and response to escalations | All operator permissions; reassigning incidents; confirming critical commands; marking false alarms; receiving escalations; viewing operator workload | S7, S8, S9 |
| `admin` | Administrator | System configuration | Catalog of objects and devices, import; connectors; correlation rules; response scenarios; SLA and escalation policies; users, roles, access scopes; retention. Does not handle incidents | S1, S2, S4 |
| `integrator` | Integrator | Developing and connecting connectors | Registering connectors and managing certificates through the approved external PKI; monitoring the status and diagnosing issues with connectors; reading raw events from sources and handling DLQ cases | S1, S3 |
| `auditor` | Auditor | Independent verification | Only reading: audit log, integrity check, incident history, export. Cannot modify anything | S10 |
| `manager` | Manager | Service efficiency assessment | Read-only: MTTA reports, MTTR, false alarms, SLA compliance, exports | S13 |

Technical entities (not users): the connector account (mTLS-certificate + Connector Gateway registration) and platform service accounts.

### 1.1 Division of Responsibilities

- The administrator cannot delete or modify audit records; any change to their own permissions is recorded in the audit.
- The auditor does not have rights to modify configurations and process incidents.
- A critical command that requires confirmation cannot be confirmed by the same user who requested it.

---

## 2. User Journeys

Each path is described in terms of steps, with details about the components involved. The failure points and expected system behavior are documented in the [acceptance criteria](acceptance-spec.md).

### J1. The integrator connects new sources (S1, S3).

1. Reads the connector developer guide and protocol contract.
2. Implements the connector with Connector SDK, mapping equipment events into raw events.
3. It runs the test set for compliance locally.
4. The administrator registers the connector and certificate fingerprint in Connector Gateway; the certificate is issued by the approved external PKI.
5. The connector connects to the Connector Gateway, completes the authenticated registration handshake and starts heartbeat messages.
6. The administrator describes the event mapping rules of the source type using the Normalizer.
7. The integrator sees in administration: the connection’s status “in the network,” event flow, and the rejection in the DLQ with reasons.

**Result:** Events from the new source have been normalized and are participating in correlation; the platform core code remained unchanged.

### J2. The administrator describes the object (S2)

1. Creates sites, buildings, floors, and zones.
2. Loading floor plans (images).
3. Imports devices from CSV or JSON: type, source, connector, zone, coordinates on the floor plan.
4. Checking import report: created, updated, line errors.
5. It arranges and adjusts devices on the floor plan.

**Result:** The catalog is published in `psim.catalog.v1`. The Normalizer enriches events in the site and zone no later than 2 seconds after the change.

### J3. The administrator configures responses to situations (S4, S7, S9)

1. It creates a correlation rule, for example: “The perimeter sensor activates, and human detection is performed by video analysis within the same zone within 30 seconds.”
2. It verifies the rule using a validator, performs a test run on historical events, and determines what signals the rule would generate.
3. Creates an incident type and a response scenario: steps, checklist, decision branches, commands, required steps.
4. Links the scenario to the type of incident, zones, and priorities.
5. It sets the SLA and escalation policies.
6. Enables the rule.

**Result:** The active set of rules has been published in `psim.config.v1`. Each processor activates the validated rule-set snapshot as a whole.

### J4. The operator processes incidents (S6, S7, S8)

1. The operator works in the console; the feed displays active incidents within their scope by priority.
2. A new incident has occurred: audio and visual alerts, and the position in the feed based on priority.
3. Receives an incident (hot key); the incident is assigned to the operator, status “received”.
4. Open the card: timeline, signals, root events, site on the floor plan, executed scenario.
5. Performs the steps of the scenario: marks the checklist, selects the decision branch.
6. The scenario step requires a command, such as “lock the door”. The operator sends the command and checks the status: “sent” -> “confirmed” -> “executed” or “failed/timeout”.
7. Adds a comment and resolves the assigned incident with an allowed outcome, such as a confirmed or false alarm.
8. Closure is allowed only after the required steps are complete.

**Result:** All actions have been recorded in chronological order and are audited.

### J5. Shift supervisor responds to escalation (S9)

1. The incident remains unacknowledged after the acknowledgement SLA deadline.
2. The shift supervisor receives notifications in the app and via email; the incident has been marked as escalated.
3. The supervisor reviews operator workload and reassigns the incident.
4. It confirms the critical command requested by the operator when necessary.
5. If there are no responses at this level, the next escalation level is triggered.

### J6. The auditor checks the log (S10).

1. It searches for audit records by user, object, incident, and time interval.
2. It initiates integrity checking; it receives results based on the chain of hashes and checkpoints.
3. Exports a sample.

**Result:** Verification reports hash-chain and signature errors with sequence positions; detecting truncation requires an independently trusted checkpoint.

### J7. The manager receives a report (S13)

1. Selects the period, sites, and incident types.
2. It shows the number of incidents, MTTA, MTTR, the percentage of false alarms, compliance with SLA, and the load on operators.
3. Exports the report in CSV format.

### J8. Operation experiences failures and updates (S11, S12)

1. In a cluster topology, a failure occurs in the Kafka broker or a service instance under load.
2. Processing is ongoing; the operator console shows a brief loss of connection and recovers without losing any changes.
3. The shift engineer sees a failure on the dashboard and an alert with a link to the runbook.
4. The update from N to N+1 is performed sequentially across services without interrupting processing.


**Result:** A cross-check confirms zero losses and duplicates.

### J9. The administrator performs maintenance (S4, S10)
1. Select a device or area within their access scope, set the completion time, reason, responsible person, and explicit suppression policies.
2. Verify the maintenance mode and remaining time on the floor plan; fire-related events and P1 continue to be processed.
3. Complete the maintenance or wait for automatic termination; check the audit and saved events.

### J10. Operators transfer responsibility (S7, S9, S10)
1. The sender operator selects open incidents, recipients, and comments.
2. The recipient checks the list and confirms acceptance; results are displayed for each incident.
3. The shift supervisor assigns the current site duty assignee and, if necessary, reassign remaining incidents.

**Result:** Responsibility is confirmed and reflected in the audit; SLAs and scenario execution continue without interruption. Shift calendars are not part of the current model.

### J11. The engineer checks the history without actions (S4, S8, S10)
1. Select a diagnostic replay, history range, and rule versions within their access scope.
2. Receive isolated signals, incidents, and simulation results from commands and notifications.
3. Compare results; production data and external systems remain unchanged. The audit retains startup parameters.

Restoring the production projection and reprocessing the production DLQ are selected separately and go through normal authorization.
