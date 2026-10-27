# Operational Functions of MVP

<!-- language-links -->
[English](operational-capabilities.md) | [Русский](../ru/architecture/operational-capabilities.md)
<!-- /language-links -->

Accepted · [ADR-042](../adr/0042-operational-capabilities.md) · [MVP Boundaries](../product/mvp-scope.md)

The document defines the permanent operational requirements. Implemented functions and limitations are listed in [Release readiness](../product/release-readiness.md).

## 1. Function Placement

| Capability | Responsible Component |
|---|---|
| Storage of Floor Plans | Resource Catalog, port `FileStore` and adapter in `libs/platform` |
| Device Connection and Status Check | Connector SDK, Connector Gateway, Normalizer, Projection Service |
| State Snapshots and Real-time Changes | Projection Service, API Gateway, web console |
| Maintenance mode | Resource Catalog, Correlation Engine, Audit |
| Duty and Responsibility Transfer | Incident Service, Notification Service, web console |
| Certificate Lifecycle | Security library, Resource Catalog, operational tools |
| Diagnostic Replay | Operational tools, isolated processors and storage |

Functions are added to existing contexts. Separate services for maintenance, shift management, and files are not introduced in MVP.

## 2. Files and Backup

In MVP, the platform stores loaded floor plans. Incident attachments remain external links; storage of photos, documents, and video clips is handled after MVP.

- The Resource Catalog stores metadata: `file_id`, site, resource owner, version, size, type, SHA-256, and object key. Binary content is accessible through port `FileStore`.
- The adapter uses AWS SDK C++ and SeaweedFS according to ADR-046 and ADR-077. Ports `FileStore`, authorization, metadata, and plan versions are implemented in Resource Catalog. The deployment supports Debian 13 x86_64, one machine, and cluster; recovery qualification requires a separate run with an independent copy.
- Bucket is closed for public reading. API Gateway checks RBAC and access scope during loading and downloading. Object key is not an access permission.
- PNG and JPEG are allowed; signature, decoding capability, size up to 20 MB, and pixel count limit are checked. SVG and executable content are not accepted. The platform generates the object name.
- The image is loaded with a new, immutable key, then PostgreSQL transaction changes the link and creates an outbox record. If failed, the previous version remains available; unreferenced objects are deleted separately after the protection interval.
- A new plan does not delete the previous one until the storage period and rollback window end. Versions referenced by active resources are not deleted.
- The backup includes metadata and objects. Exercises on a fresh stack check links, versions, and SHA-256; a backup in the same bucket is not considered an independent copy.

The port allows changing the storage provider without modifying the catalog and REST API.

## 3. Device Status and Data Quality

Catalog states `active`, `disabled`, `archived` indicate the resource lifecycle. The operational model separately contains:

| Measurement | Content |
|---|---|
| Connection | `online`, `offline`, `unknown`; connector and device availability are shown separately |
| Observed Status | Values sent by the device, such as alarm, fault or armed state |
| Quality | `actual`, `stale`, `unknown` |
| Origin | Source, epoch, sequence, event and receipt time, projection version |
| Last Acknowledgment | Time of the current observation or status check |

Rules:

1. A connector connection does not prove that all its devices are healthy. The SDK provides device availability if the external protocol allows it to determine.
2. Missing three agreed heartbeat intervals changes the connection state to `offline`; related data loses quality `actual`. Device freshness is determined separately by source type.
3. Until the first acknowledgment, the value is `unknown`. After the freshness limit expires, it becomes `stale`; the last known value and time are retained but not displayed as the current valid status.
4. After reconnection, the SDK checks the status if the device supports snapshot requests. For events during the check, the source sequence boundary is recorded; an older snapshot does not overwrite a newer confirmed state. If a snapshot cannot be obtained, quality remains `unknown` until the next valid observation.
5. Pushing old events from the buffer adds to the history but does not make the status current. Repeated and re-ordered events do not reduce the projection version.
6. During reconstruction, freshness is recalculated based on current time and source acknowledgments. Kafka replay does not create false `online`.
7. Correlation rules requiring the current state take data quality into account. Unknownness is not considered a normal state.

State snapshots and changes are accessible via REST and WebSocket with the same access scope. The scheme “snapshot + delta + resume token” from ADR-019 is used, supplemented by device status, zones, and connectors. The p99 delay from updating the projection to getting the delta is no more than 200 ms; the browser shows quality and last acknowledgment time.

## 4. Maintenance mode

Resource Catalog stores `MaintenanceWindow`: device or zone, start and end time in UTC, reason, responsible party, access scope, version, and explicit suppression policy. End time is required; MVP supports one-time intervals. Scheduled recurring maintenance is after MVP.

- Activation and modification are available to the administrator within their access scope; all changes and automatic completion are recorded in audit.
- The policy explicitly lists types of events or rules for which new signals are suppressed. Raw and normalized events are retained, suppression is accounted for in metrics and history.
- Fire events and P1 priority signals are not suppressed in MVP maintenance mode.
- Already opened incidents are not closed; their SLA and commands are not automatically canceled.
- The start and end moment are recorded by the platform and applied to new events based on their receipt time; reloading the history does not include maintenance retroactively.
- After the window expires, it automatically ends, including recovery after restart. The limit is accurate within 1 s. The interface shows objects under maintenance and remaining time.
- Interval and version history policies are needed to explain why an event triggered or did not trigger a signal.

## 5. Duty and shift handover

Minimum MVP requirements:

1. The shift supervisor explicitly assigns the current duty officer to their site. The user role and access scope are verified during assignment and transfer.
2. The handing-over operator selects unclosed incidents, recipients, and comments; the recipient confirms acceptance. Until confirmation, responsibility remains with the previous operator.
3. Each incident is handed over as a separate atomic command with version verification; batch handover shows results per row. A repeated request does not hand over again the same incident.
4. Handover does not reset SLA, restart scenarios, or change executed steps. Completed acknowledgments of critical commands are retained; self-approval remains prohibited.
5. If the recipient is unavailable, the access scope has changed, or the version is outdated, handover is rejected with a reason. The shift supervisor can explicitly reassign with audit.
6. Escalation resolves the current duty assignee when sending; if no duty officer is available, it uses a configured backup recipient and creates an operational alert.

The full shift schedule, recurring schedules, leave, and automatic replacements are available after MVP. Time is stored in UTC, and the interface uses the site’s time zone.

## 6. Certificates, secrets, and access restoration

Requirements SR-01, SR-09, and SR-26 remain fundamental. MVP uses the platform operator’s CA or existing PKI organization; no custom certificate authority service is developed.

Regulations and tools include:

- Initial trust setup and connector registration via CSR and fingerprint; a private key is created on the connector side and not transferred to the platform.
- Renewal with overlapping certificates and verification of no event loss.
- Revocation with closure of active sessions, prohibition of new connections, and auditing.
- Warning when 30 days are left and tracking of applied secret versions without disclosing secret values.
- Rotation of inter-service certificates and secrets without bypassing trust verification.
- Restoration of Keycloak configuration, keys, and trust from a secure backup; if the key is compromised, reissue is required, not restoration of the old key.
- Emergency access via SR-29 and exercises on a clean test environment.

For external PKI, step-ca with step CLI is selected ([ADR-043](../adr/0043-platform-security.md)); existing organizational PKI are also acceptable. Mechanisms and procedures are described in [trust regulations](../security/platform-trust.md). Both topologies are described in [PKI guide](../engineering/deployment-pki.md).

## 7. Isolated diagnostic replay

Diagnostic replay is used to test rules and analyze history. Normal recovery of working projections and reprocessing DLQ are separate explicit operations.

- Each diagnostic run has `replay_id`, time range, rule version, configuration, initiator, and access scope. This is reflected in auditing.
- Reading is limited to the initiator’s scope; results are written in separate topics, consumer groups, and storage with limited lifetime.
- The executor of the diagnostic run has no rights to write into working topics for commands, notifications, signals, and incidents, nor access to external scenario executors. Isolation is ensured by ACL and adapter configuration, not just `dry_run`.
- Diagnostic commands and notifications are saved as simulation results; no HTTP, SMTP, or device commands are sent.
- Transferring results to the production environment is not automatic. Adding new rules is an administrator’s action.
- Restoring projections does not trigger the Response Engine. Reprocessing of working DLQ shows scope and potential consequences, requiring explicit selection of working operations with normal checks.

Verification uses history that would generate commands, emails, and webhooks in the production environment. Before and after replay, topics of signals, incidents, commands, and notifications, working incidents, and external call counts are compared: changes and calls must be 0. Audit records of the actual run are retained.

## 8. Contracts and verification

State and reconciliation, maintenance, files, responsibility transfer, subscriptions, and snapshots are described in [public contracts](../../contracts/README.md). Field names and semantics are taken from contract versions compatible with the installed platform.

Extensions are performed according to [compatibility rules](../engineering/compatibility-and-support.md), pass `contracts:check`, and Schema Registry verification. Versions of aggregates and field names in existing contracts retain their original meaning. Adding delta types is consistent with client capabilities; clients without support receive previous streams.

Acceptance tests complement S1, S2, S4, S6-S11. Details - [acceptance-spec.md](../product/acceptance-spec.md), methods - [verification registry](../quality/verification.yaml).
