# ADR-053. Incident Service

<!-- language-links -->
[English](0053-incident-service.md) | [Русский](../ru/adr/0053-incident-service.md)
<!-- /language-links -->

- Status: Accepted
- Related decisions: ADR-008, ADR-020, ADR-027, ADR-042

## Context

The previous implementation created incidents without a full lifecycle. Commands from the operator, permissions, type versions, confirmed transfers, and durable query results were required. The original invariants I3 and I4 were contradictory: a new grouping window could begin while the previous incident remained open, but a new incident with the same key was prohibited.

## Solution

1. Maintain five states for ADR-027. Transitions and restrictions are handled by the pure library `libs/domain/incident`; the executor passes values and checks permissions through the common library `libs/platform/authz`. Data, timeline, inbox, REST query results, and outbox are stored in a single PostgreSQL transaction.
2. Serialize signals by locking the tenant and grouping key; changes to the aggregate are serialized by a row lock. A partial unique index allows one signal-receiving slot per key. After the window expires, the slot is transferred to a new incident with `related_incident_id`; the previous incident remains open with the same responsible person. Resolution and closure release the slot; reopening does not displace a newer incident.
3. The inbox retains the identity of the signal and the fingerprint of the body. Complete signals are stored in a separate table and read page by page. The field `signal_ids` contains no more than 1000 identifiers; the full size and truncation are specified in `signal_count` and `signal_ids_truncated`. Increasing the number of signals does not stop processing or create new windows.
4. Types have immutable versions, grouping window, priority matrix, and closing delay. The version is fixed when an incident is created; subsequent signals and closures use it. The choice of type and key is sent by the signal from the output configuration of the rule. Changes to types are published in `psim.config.v1`.
5. REST requires a verified JWT, UUID Idempotency-Key, and If-Match to modify an existing incident. The query result is stored by tenant, subject, and key. A repeat returns the previous result after an updated access check; other content is rejected. In case of version conflict, changes and audit of successful actions are not saved; access failures are stored independently.
6. The handover recipient is verified through Keycloak: enabled, operator/supervisor role, tenant and access scope. The reading client has `view-users` and `view-clients` without modification rights; its secret is sent in a secure file. The transfer packet consists of independent transactions. The request does not change the responsible person; confirmation requires the recipient, transfer_id, and version. Any other change to the aggregate makes the previous confirmation obsolete. Acceptance time, scenario version, and SLA are not lost.
7. Automatic closure uses the saved PostgreSQL window and locks `SKIP LOCKED`. Multiple instances do not duplicate closure. The time source is outside the domain; transitions after the clock moves backwards, violating time order, are rejected.
8. The minimum projection and browser feed accept all variations of the existing event contract. A specific local exception `legacy_types` retains the original laboratory flow without type configuration; by default, the exception is disabled.

## Verification

`task incidents:test`, `task incidents:bench`, `task incidents:sanitizers`, `task incidents:coverage`, `task incidents:sim`, and end-to-end `task flow:test`. Verifications use two actual instances, Kafka, PostgreSQL, and Keycloak. Full console and integration with the response scenario are defined in ADR-071 and ADR-012.
