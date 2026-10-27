# Acceptance Specification

<!-- language-links -->
[English](acceptance-spec.md) | [Русский](../ru/product/acceptance-spec.md)
<!-- /language-links -->

Scenarios S1-S13 are described in the format “given - when - then”. Each criterion is verified: it has a measurable threshold and a verification method. The methods and test environments are defined in the [Quality Strategy](../quality/strategy.md); a full run is required for qualification release.

Qualification of the complete load, failure and recovery profile remains outstanding. Current evidence and limitations are recorded in [release readiness](release-readiness.md). HA qualification requires replicas in independent physical failure domains.

## General Conditions

**Test Environments:**

| Code | Test Environment | Purpose |
|---|---|---|
| `E-LOCAL` | One Linux host, native Docker Engine with Docker Compose, one Kafka broker | Functional scenarios |
| `E-CLUSTER` | Cluster on VM: 3+ nodes, Kafka ×3, PostgreSQL with synchronous replication, Valkey with primary and two replicas, and Sentinel | Load, failures, updates |

**Background load `LOAD-50K`:** The simulator generates 50,000 events per second, with 10,000 virtual devices on 10 sites with uneven profiles: 30% of the load is allocated to one site. Each event has a sequence number of the source for cross-checking.

**Latency is measured by event time:** from the time the event originates at the source to the time it is observed (signal in the topic, display of the incident in the browser). The clocks of the test environment is synchronized, with a deviation of ≤ 5 ms.

**Verification methods:** `E2E` - automatic end-to-end testing (Playwright or API test); `LOAD` - load test; `CHAOS` - fault test; `SIM` - deterministic simulation; `DOC` - verification by a third-party executor according to documentation.

---

## S1. Connecting a new connector

**Method:** E2E + DOC · **Test Environment:** E-LOCAL

```gherkin
Given the platform is deployed and its core has no source-type X implementation
And an integrator implemented a connector with Connector SDK using the guide, without reading core code
When the connector runs the conformance suite
And an administrator registers it and its certificate fingerprint in Connector Gateway
And the certificate is issued by the approved external PKI
And an administrator defines normalization mappings for source type X
And the connector connects and sends events
Then the conformance suite passes without errors
And administration shows the connector online within 5 seconds
And normalized events appear in psim.events.normalized.v1
And no core service images or configurations change, except the mapping rules
```

**Negative checks:**
- A connector without registration or with invalid certificate is rejected during connection, and the failure is recorded in audit;
- An event that does not pass schema validation is rejected with an error code from the catalog and does not enter Kafka;
- An event without a mapping rule enters `psim.dlq.normalizer.v1` with a reason.

**Certificate lifecycle (F-PLT-08):**
- Renewal with an overlap of two certificates does not lose events; after the overlap ends, the old certificate loses access;
- Revocation closes the current session and blocks reconnection; registration, renewal and revocation are reflected in audit;
- The private connector key is not transferred to the platform and does not enter diagnostic materials.

---

## S2. Catalog Setup

**Method:** E2E · **Test Environment:** E-LOCAL

```gherkin
Given an administrator is signed in
When they create a site, a building, 3 floors and 20 zones
And upload floor plans
And import 50,000 devices with zone, source and connector bindings
Then import completes within 5 minutes with created, updated and per-row error results
And devices appear at their floor-plan coordinates
And catalog changes reach Normalizer within 2 seconds of commit
```

**Negative checks:**
- Lines with a non-existent zone or duplicate identifier are reported with an error and do not interrupt the import of other lines;
- Reimporting the same file does not create duplicates (idempotence by external identifier).

**File storage (F-PLT-07):**
- PNG/JPEG up to 20 MB are only loaded and read by the user within the authorized scope; fake formats, SVG, exceeding size and pixel limits are rejected;
- Changing the plan creates a new version; errors in loading or metadata storage retain the previous available version;
- Direct public reading of buckets is prohibited; metadata and objects are restored on a clean test environment with matching SHA-256 and links.

---

## S3. Normalizing and enriching events

**Method:** E2E + LOAD · **Test Environment:** E-LOCAL, E-CLUSTER

```gherkin
Given a sensor is registered in zone Z at site P
When its connector sends a raw activation event
Then psim.events.normalized.v1 contains an event with a taxonomy v1 type,
     severity, site_id = P, zone_id = Z, device_id and the original source sequence number
And its trace context is linked to the ingestion trace
And normalization-stage p99 latency is at most 30 ms under LOAD-50K
```

**Negative checks:**
- A repeated event with the same sequence number of origin does not create a second normalized event;
- An event from a device not in the catalog is sent to DLQ with a reason.

---

## S4. Correlation of multiple source events

**Method:** E2E + LOAD + SIM · **Test Environment:** E-LOCAL, E-CLUSTER

```gherkin
Given a rule matches perimeter activation and video person detection in the same zone within 30 seconds
And its output is a high-priority intrusion signal
When the perimeter activates in zone Z and video detects a person there within 10 seconds
Then psim.signals.v1 contains exactly one intrusion signal referencing both events
And event -> signal p99 latency is at most 300 ms under LOAD-50K
```

**Additional checks:**
- The same events 40 seconds apart do not produce a signal;
- Events from different zones do not produce a signal;
- An event that arrives late within acceptable limits is considered by event time;
- Rules with syntax errors cannot be activated;
- A trial run of the rule on historical data shows signals that the rule would generate, without publication in `psim.signals.v1`.

**Maintenance (F-CAT-06):**
- The administrator sets a one-time interval for a device or zone with a reason, responsible person, and explicit policy; only selected new signals are suppressed, events are retained;
- Fire incidents and P1 are not suppressed; open incidents and their SLA do not change;
- After the interval ends, suppression stops with an accuracy of ≤ 1 second, including restarting; rules consider policy versions and provide explanations for suppression;
- If the status is unknown or stale, it is not considered a normal state of the device.

**Diagnostic replay (F-PLT-09):** The history that would trigger commands and notifications is processed in an isolated run; production signals and incidents do not change, external calls are 0. Initiator, scope, versions, and range are reflected in audit.

---

## S5. A signal creates an incident without duplicates

**Method:** E2E + SIM + CHAOS · **Test Environment:** E-LOCAL, E-CLUSTER

```gherkin
Given no open incident accepts signals for grouping key K
When a signal with key K arrives
Then exactly one incident in the new state is created
When another signal with key K arrives within the grouping window
Then it is attached to the existing open incident
And priority and timeline are updated without creating another incident
```

**Stability:** In simulations with errors (repeated delivery, reordering, failure of Incident Service and Correlation Engine at any point, rebalancing) on 10,000 runs, the number of incidents matches the reference, with 0 duplicates.

---

## S6. Incident in the Console Within One Second

**Method:** E2E + LOAD · **Test Environment:** E-CLUSTER

```gherkin
Given an operator with site P access has opened the console
And LOAD-50K is running
When an event at site P creates an incident
Then it appears in that operator's feed within 1 second of the event at p99
And a priority-specific sound and visual indication are emitted
And the feed positions the incident according to priority
```

**Additional checks:**
- An operator scoped to “site Q” does not see this incident in the feed or through REST API;
- A feed with 10,000 active incidents is scrolled at a frequency of ≥ 50 frames per second.

**Device and realtime status (F-INT-08, F-API-02, F-API-03):**
- On the floor plan, availability, last observed state, quality actual/stale/unknown, and confirmation time are separately visible;
- Snapshots and deltas of devices, zones, and connectors are filtered by access scope; p99 delivery deltas after projection update ≤ 200 ms;
- After a break in WebSocket, the status is restored without skipped or duplicate versions; old clients receive only supported streams;
- A connector connection itself does not prove that its device is healthy or its data up-to-date.

---

## S7. Accepting an incident and executing a scenario

**Method:** E2E · **Test Environment:** E-LOCAL

```gherkin
Given intrusion incidents use response plan version 3:
     instruction -> checklist -> decision "Alarm confirmed?" -> yes: command + notification
                                                         -> no: mark false alarm
And the step "Report to shift supervisor" is mandatory
When an operator acknowledges a new intrusion incident
Then it is assigned to that operator, becomes acknowledged and uses response plan version 3
When the operator completes the checklist and selects yes
Then execution advances to the command step and records actions in the timeline
When the operator tries to resolve the incident before completing the mandatory step
Then the operation is rejected and identifies that incomplete step
```

**Stability:** After restarting the Response Engine mid-script, execution continues from the same step; the script version remains 3, even if the administrator published version 4.

**Transfer of responsibility (F-INC-07):**
- The operator proposes transferring an unclosed incident; the recipient confirms it within their scope, and until confirmed, the previous operator remains responsible;
- Repeating a request does not transfer repeatedly; outdated versions, inappropriate roles, and foreign scopes are rejected.
- SLA, scenario version, current step, and completed actions are preserved; the request, acknowledgment, and reassignment by the shift supervisor are visible in the audit.
- Batch transfer shows the result for each incident without any hidden partial transfers.

---

## S8. Device Command

**Method:** E2E + CHAOS · **Test Environment:** E-LOCAL

```gherkin
Given door D supports the lock command
And its connector is online
When an operator sends lock from a response step
Then command state progresses requested -> sent -> acknowledged -> executed
And request -> connector delivery p99 latency is at most 200 ms
And the result appears in the incident card and timeline
```

**Failure and timeout:**
- The connector rejects the request - the command receives a failed status with a reason.
- The connector is not responding - after the timeout period, the command receives the “timeout” status.
- Each command ends with a final status.

**Negative Tests:**
- Re-sending with the same command_id does not result in double execution.
- The operator without command rights encounters a failure, which is recorded in the audit.
- The critical command cannot be executed on the device without confirmation from the shift supervisor; the initiator cannot confirm it themselves.

Isolation of diagnostic commands (F-PLT-09): Running a replay with critical command history only produces simulation results. The number of production commands and external calls does not increase; attempts by the executor to record in the production topic are rejected by the ACL.

---

## S9. Escalation in Case of SLA Violation

**Method:** E2E + SIM · **Test Environment:** E-LOCAL

```gherkin
Given an acknowledgement SLA of 60 seconds
And escalation level 1 targets the shift supervisor and level 2 the manager after a further 120 seconds
When an incident remains unacknowledged for 60 seconds
Then it is marked escalated no later than 61 seconds after creation
And the supervisor receives in-app and email notifications
And notification delivery is audited
When there is no response for a further 120 seconds
Then escalation level 2 is triggered
```

**Additional checks:**
- An incident acknowledged before its SLA expires is not escalated.
- During a mass event (≥ 100 incidents per minute), notifications are grouped together instead of being sent individually.
- Restarting the timer service does not cancel or duplicate the escalation.

**Site Duty Assignee (F-RSP-08):**

- The shift supervisor explicitly assigns the current duty holder to their area; each new escalation resolves the current recipient.
- The shift of a duty holder does not cancel the SLA period or the already sent notifications.
- When no one is on duty, the specified backup recipient is used, and an operational alert is generated.

---

## S10. Audit Integrity

**Method:** E2E · **Test Environment:** E-LOCAL

```gherkin
Given scenarios S1-S9 have been executed
When an auditor opens the audit log
Then it includes user and system actions from S1-S9,
     including login, access denials, configuration changes, commands and escalation
When a record is modified or deleted directly in the database
And the auditor runs integrity verification against independently trusted checkpoints
Then verification identifies the affected record or sequence gap
```

**Additional checks:**
- The auditor has no mutating operations in either the UI or API.
- The exported sample contains data for independent verification of the hash chain.

**Operational Actions:** The log records certificate registration, renewal, and revocation, as well as the automatic termination of maintenance, assignment of duty holders, and responsibility handover operations and replay parameters. Unauthorized actions are rejected and recorded; secrets and file contents are not logged.

---

## S11. Failure Recovery Without Loss

Method: CHAOS · **Test Environment:** E-CLUSTER

```gherkin
Given LOAD-50K is running
When a Kafka broker, each service instance, a PostgreSQL node and a Valkey node fail in turn
Then source-sequence reconciliation finds 0 lost events
And incident counts match the reference with 0 duplicates
And processing resumes within 60 seconds (RTO), with 0 lost committed incidents (RPO)
And the console reconnects without losing changes
And each failure appears on a dashboard with an alert linked to its runbook
```

**Additional:** A connection to the platform is interrupted for 10 minutes - buffered events are replayed after reconnection without loss.

Reliability and Recovery (F-INT-08, F-PLT-07, F-PLT-08):

- After three heartbeat intervals have passed, the connector becomes offline; the associated data loses its “actual” quality.
- Replaying ten-minute-old history does not restore freshness; only the latest snapshot or newly confirmed observation restores quality. An old snapshot does not overwrite new events.
- A source without an available snapshot remains unknown until an actual observation is made; rebuilding the projection based on old history does not provide false freshness.
- The exercises on the clean test environment restore file objects, links, Keycloak, and trust; a revoked certificate no longer has access after restoration.

---

## S12. Rolling Upgrade

**Method:** CHAOS + E2E · **Test Environment:** E-CLUSTER

**Status:** The load qualification has not been completed. A successful execution is required before release; the following criteria have not been relaxed and have not yet been confirmed. Known limitations are listed in [release readiness](release-readiness.md).

```gherkin
Given the cluster runs version N under LOAD-50K
When services are upgraded sequentially to N+1 with database schema migrations
Then event processing continues without a gap longer than 5 seconds between new signals
And reconciliation reports 0 losses and 0 duplicates
And event -> incident p99 latency remains at most 1 second throughout the upgrade
When N+1 is rolled back to N
Then the same criteria hold
```

---

## S13. Manager’s Report

**Method:** E2E · **Test Environment:** E-LOCAL

```gherkin
Given 30 days of history with known reference values
When a manager requests a report across all sites for those 30 days
Then it is generated within 3 seconds
And incident counts, MTTA, MTTR, false-alarm ratio and SLA compliance match the reference
And CSV export contains the same values
```

**Definitions of metrics:** MTTA - Mean Time To Acknowledge; MTTR - Mean Time To Resolution; False Alarms Ratio - The proportion of incidents resolved with the result “false” among all resolved incidents during a period.

---

## Traceability Matrix

| Scenario | Roles | Paths | Functions | Method |
|---|---|---|---|---|
| S1 | integrator, admin | J1 | F-INT-01..04, 06, 07; F-EVP-02; F-IAM-03; F-PLT-08 | E2E, DOC |
| S2 | admin | J2 | F-CAT-01..05; F-PLT-07 | E2E |
| S3 | - | J1 | F-INT-04; F-EVP-01..04 | E2E, LOAD |
| S4 | admin | J3, J9, J11 | F-EVP-06..08; F-CAT-06; F-PLT-09 | E2E, LOAD, SIM |
| S5 | - | - | F-EVP-08; F-INC-01..03 | E2E, SIM, CHAOS |
| S6 | operator | J4 | F-API-02, 03; F-WEB-02, 03; F-IAM-02; F-INT-08 | E2E, LOAD |
| S7 | operator, supervisor | J4, J10 | F-INC-01, 04..07; F-RSP-01..04 | E2E |
| S8 | operator, supervisor | J4, J5 | F-RSP-05; F-INT-01; F-CAT-02; F-PLT-09 | E2E, CHAOS |
| S9 | supervisor, manager | J5, J10 | F-RSP-04, 06..08 | E2E, SIM |
| S10 | auditor | J6, J9-J11 | F-AUD-01..03; F-CAT-06; F-INC-07; F-RSP-08; F-PLT-08, 09 | E2E |
| S11 | - | J8 | F-INT-02, 04, 08; F-EVP-08; F-PLT-01, 04, 07, 08 | CHAOS |
| S12 | - | J8 | F-PLT-02, 06 | CHAOS, E2E |
| S13 | manager | J7 | F-REP-01; F-WEB-05 | E2E |
