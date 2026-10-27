# ADR-068. Server Operations for the Real-time Interface

<!-- language-links -->
[English](0068-operational-interface-backend.md) | [Русский](../ru/adr/0068-operational-interface-backend.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-059](0059-console-projections.md), [ADR-060](0060-realtime-delivery.md), [ADR-061](0061-live-access-validation.md), [ADR-064](0064-alarm-philosophy.md), [ADR-067](0067-assistant-boundaries.md)

## Solution

The Incident Service stores independent indicators of condition and alarm acknowledgement, shelving, suppression, and presence. Changes require valid rights, `If-Match`, and `Idempotency-Key`. Changing conditions and suppressing events can be done by the shift supervisor. Shelving and suppression require a cause and must be completed within one day. They do not close an incident or suspend SLAs. A new condition, upon return, cancels the alarm acknowledgment.

Presence is tied to the user and session. The lease is limited to 60 seconds and a JWT expiration time. A new lease gets a new identifier; an outdated identifier does not renew the lease. Up to 16 sessions and one editing participant are allowed per incident. The lock is advisory: normal incident actions check rights and version regardless of presence. PostgreSQL serializes changes between instances. Expiration stores the new version and publishes events through the outbox. Audit keeps specific actions, cause, version, and control timestamps; these records are not limited to the mutable incident timeline.

All events in the new state contain the full incident and go through existing projections and realtime. Private assistant responses and training states are not published to working topics. Expiration of time indicators is processed without new signals. Shift handover confirmation preserves the existing recipient and version protocol. SLA deadlines are taken from the original `SlaPolicyPinned`, and risk is calculated relative to the time when the incident was created: the last 20% of the duration, but not less than one second, indicates risk; reaching the duration means a violation. Lack of original policy means unknown risk.

Incident snapshots are stored by the trigger in the same transaction. The Projection Service independently stores the history of applied catalog images, incidents, observations, maintenance and duty assignments. Historical queries use server-time timestamps, current user access scopes, and historical resource versions. It does not access current lease connectors, does not issue realtime tokens, and does not confirm current availability. Initial deployment history is available from the time of migration; rebuilding a projection retains the previous history and adds a new source image when switching generations.

The Command Service provides a separate preview with the same validation rules for parameters and access scopes as the command request. Consequences are defined by the administrator in capability; absence of description means unknown consequences. The preview does not create a command or send a message. The preview’s fingerprint can link the request and acknowledgment to catalog versions; an outdated fingerprint is rejected. Actual sending rechecks the configuration. A critical command requires another shift supervisor and the current version of the command.

A training session contains a copy of the allowed incident with a separate synthetic identifier, belongs to one user, has its own version, and expires after an hour. Ten user sessions are allowed at the same time. Evaluation is determined by the sequence: acknowledgment -> start working -> training command -> resolution. The training command only changes the training state. Audit contains the session identifier; working incidents, Command Service, and Response Engine do not change.

The assistant does not store dialogues. Each request selects anew the current incident in the user access scope, stores the version, and contacts the local provider configured by the administrator using Ollama protocol `POST /api/chat`. The provider does not receive bearer tokens or tools. The response is limited in size, time, and number of conclusions; unknown sources, additional executable fields, and incorrect JSON are rejected. Interpretations are marked as assumptions. Access to the model is done without blocking the incident; a new version of the source rejects the result. After the response, the original identity expiry and user’s current record in the catalog are rechecked. If the provider is unavailable, the allowed original data remains. Audit keeps the user, request, provider, model, source version, and response. Protocol checks do not replace quality evaluation of a specific model.

## Consequences

Lease, alarm, and lifecycle use the same version of the incident. Changing presence can cause version conflicts in working actions; the client rereads the current image. Changes to presence and expiration only of its lease advance the version of a valid handover without changing the briefing; the recipient confirms the current version. Changing working data disables the previous briefing and requires regeneration of the handover. The latest change to presence does not restore the invalidated handover.

Historical rewind shows the state known to the server at the time of recording. It does not restore unknown states before the source was connected. Historical images are not used as commands to change the working state. Operational policies for storing history and performance of the full profile are checked together with deployment and load tests.

## Verification

`task operations:test`, `task commands:test`, `task projection:test`, `task realtime:test`; domain module checks, contract verification, ASan/UBSan, TSan, architectural constraints, and coverage. Repeats are checked separately on another instance, lease expiration, version conflict, rejected model source, unavailable provider, and lack of working effects of exercises.
