# Operational Interface Backend

<!-- language-links -->
[English](operational-interface.md) | [Русский](../ru/engineering/operational-interface.md)
<!-- /language-links -->

Solution: [ADR-068](../adr/0068-operational-interface-backend.md). Contracts: [real-time operations](../../contracts/proto/psim/api/v1/operations.proto), [projections](../../contracts/proto/psim/api/v1/feed.proto), [commands](../../contracts/proto/psim/api/v1/commands.proto).

## Alarms and Presence

`GET /v1/incidents/{id}/operations` returns the current incident, its source version, evaluation time, and SLA risk. The alarm status is indicated by `incident.operational`: independent `active` and `acknowledged`, author of confirmation, time of condition change, individual `shelved` and `suppressed` with cause, author, and end time.

`POST /v1/incidents/{id}:update-alarm` accepts `action`: `activate`, `return`, `acknowledge`, `shelve`, `unshelve`, `suppress`, `unsuppress`. For condition change and suppression, a supervisor is required; the operator can acknowledge or shelve the alarm. Temporary control requires `reason` and `until`; cancellation requires the cause. Audit retains specific actions, cause, version, independent alerts, and control periods. The period is limited to one day. Acknowledging an alarm does not transition the incident to another lifecycle stage.

`POST /v1/incidents/{id}:set-presence` accepts UUID `sessionId`, flag `editing`, and `ttlSeconds` from 1 to 60. The first request does not contain `leaseId`; the response returns a new lease in `incident.operational.presence`. Extension and cancellation use this identifier; for cancellation, `release: true` is sent. An expired lease cannot be restored by repeating the previous request. All operations require UUID `Idempotency-Key` and current `If-Match` of the incident. A new version is applied to existing realtime. The client agrees on `incident_event` and `incident_operational_updated`; previous clients do not receive the new version of the event. Having a lease does not grant business rights. `conditionKnown: false` indicates no observation of the condition for previously created incidents.

Assignment and confirmed transfer use existing incident operations. SLA is not recreated during transfer. Presence does not disable unchanged briefings, but requires current `If-Match`. Changing working data requires a new handover offer; extending a lease does not restore the previous briefing. Risk `unknown` indicates absence of original targets; `on-time`, `at-risk`, `breached`, `completed` are calculated by the server. `at-risk` corresponds to the remaining 20% of the original period, but not less than one second of warning.

## History and Restoration

`GET /v1/incidents/{id}/snapshots` returns up to 200 versions. `afterVersion` and `nextVersion` set pagination. Parameter `at` selects the last image at the specified server time. The response contains `historical: true` and time of each image capture. Historical version is available only to users with rights to the current incident and historical scope.

`GET /v1/incidents/{id}/recovery?knownVersion=...` returns the current incident, number of changed versions, and limited summary of chronology. `snapshotRequired` requires replacing local state completely if the original version is unknown or the range exceeds the summary limit.

`GET /v1/feed/snapshot?at=...&locationId=...` returns the consistent historical snapshot of the feed, object status, and catalog versions for device placement on the floor plan. `siteIds` limits selection. Historical response does not contain subscription tokens and does not check current connections; it does not prove the device’s availability now. `availableFrom` sets the start of available history. Requests before the start of history are rejected. History is retained when the projection generation is switched.

## Preview of Command

`POST /v1/commands:preview` accepts object `command` with fields of regular command request. It returns consequence description, unknown consequence indicator, need for second shift supervisor, device version and type, configuration fingerprint. Rights, parameters, device status, and incident are checked without recording the command and sending to Kafka.

Field `Capability.consequence` is filled by the administrator. An empty description is displayed as an unknown consequence. Preview does not reserve the device. The client sends received `fingerprint` as `previewFingerprint` when requesting and confirming: changing catalog version rejects the operation. Field is not required for compatibility with existing clients. During actual request and confirmation, rights, command version, and capability availability are rechecked. The confirming party must differ from the initiator and have role supervisor in the required scope.

## Exercises

`POST /v1/incidents/{id}:create-exercise` with `title` creates a private training session for one hour. `GET /v1/incidents/{id}/exercises/{exerciseId}` returns it to the owner. `POST .../{exerciseId}:act` accepts `action` and mandatory `reason`; `If-Match` refers to version of the training session. Order: `acknowledge` -> `start-work` -> `command` -> `resolve`. Each step adds 25 points.

The training incident has a separate synthetic identifier, unknown to production incident and command APIs. Changes are stored in `incident.exercises`, have their own audit, and do not change production incidents, SLA periods, and devices. A user can have up to ten active sessions. Expired sessions are removed in bounded batches.

## Local Assistant

Settings of Incident Service: `settings.assistant_endpoint` - full address of local `POST /api/chat`, `settings.assistant_model` - fixed identifier of installed model. TLS uses `settings.jwks_ca_file`; regular HTTP is allowed only with explicit `settings.allow_plaintext` in isolated development environment. Empty settings disable the model with access to original data preserved. The model is installed by the provider administrator in advance; operator requests do not load the model or select the address.

`POST /v1/incidents/{id}:ask-assistant` accepts only `question` and requires `If-Match` version of the source. Each request gets current allowed facts again. Fields `sources` contain identifier, version, and time of receipt; `claims` contains text, link to server source, and assumption indicator. The client displays text with mandatory marking of origin. The response does not contain executable action.

The adapter implements the [Ollama Chat protocol](https://docs.ollama.com/api/chat): `stream: false`, `format: json`, without tools. No more than two model requests can be sent to an instance simultaneously; exceeding this limit will result in a 429 error. The deadline is 5 seconds, with responses up to 32 KiB and no more than 12 claims of 2000 characters each. Incorrect JSON, external links to sources, additional executable fields, and transport failures will result in `unavailable: true`. Available sources are retained. Audit records include the request identifier, user, model, source version, and accepted response. There are no dialog logs or cross-user caches.

Integration testing uses a local HTTP provider to verify the protocol and access boundaries. The quality and speed of generating the selected working model are tested separately on the infrastructure where it is deployed.

## Tests

- `task operations:test` - two Incident Services, PostgreSQL, Kafka, Keycloak, and a local protocol provider.
- `task operations:sanitizers` - the same set with ASan/UBSan and TSan.
- `task operations:coverage` - combined coverage of domains and services with incident regression, commands, projections, realtime, and reports.
- `task commands:test` - preview, confirmation by another user, and delivery through the Connector Gateway.
- `task projection:test` - historical feed and plan, filters, absence of confirmation for current connections, and rebuilding.
- `task realtime:test` - delivery of full snapshots, restoration, and current access scopes.

Metrics `psim_incident_operations_total` and `psim_incident_operations_seconds` use limited tags for operations and results. The projection panel shows request frequency and p99; alerts identify operation errors and agent unavailability.

## Errors and Operation

When the client reaches 412, it rereads the current snapshot and shows the changes before requesting confirmation again. For commands, preview is also repeated; the previous snapshot is not automatically replaced. An expired lease is created again with a new identifier and current incident version.

429 indicates that two slots of the instance are occupied. Normal operations and source reading remain separate requests. `unavailable: true` is checked by the result metric: network failure checks the configured address, TLS, and model presence; incorrect response checks JSON and links to server sources. Model text is not used to select an address or execute commands.

Without a historical snapshot, the current state cannot be used to replace the selected time. `availableFrom`, selected site, projection completeness, and PostgreSQL status are checked. Rebuilding the projection does not delete existing snapshot history. The full load profile, storage periods, and operational topology are checked during deployment and verification phases.
