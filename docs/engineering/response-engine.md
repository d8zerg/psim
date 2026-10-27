# Execution of Response Scenarios

<!-- language-links -->
[English](response-engine.md) | [Русский](../ru/engineering/response-engine.md)
<!-- /language-links -->

The Response Engine executes scenario versions, stores execution states in PostgreSQL, and publishes transitions to Kafka. The pure interpreter is located in `libs/domain/response`, the service is in `services/response-engine`, and the saved timers are in `libs/platform/timers`. Solution: [ADR-012](../adr/0012-response-execution-and-timers.md).

## Plans and Bindings

The HTTPS API provides `GET /v1/plans`, `GET /v1/plans/{id}`, `POST /v1/plans`, `POST /v1/plans:validate`, `POST /v1/plans/{id}:publish`, and `POST /v1/plans/{id}:archive`. The body of saving and checking is the ResponsePlan object. Publishing takes a version number. Saving creates the next draft version; the published version is not editable. Archiving closes all versions of the plan for new runs and saves previously started runs.

The list supports filters `incidentTypeId`, `includeArchived`, `pageSize` up to 1000, and `pageToken`. Reading a single plan requires `version`; zero means the last version. Changing plans is available to admins with the scope `*`. The global list of plans also requires the scope `*`; the operator gets the desired version along with the available run.

The graph contains up to 200 reachable steps without loops and one initial step. The Decision contains at least two options with non-empty links; ending a branch is determined by the final step. Types: instruction, checklist, decision, command, notify, wait, escalate, automated. Instructions can be up to 8000 bytes, checklists up to 200 items, waiting up to 30 days, and handlers with deadlines of 1-30 seconds. The entire plan is limited to 512 KiB.

Binding is selected based on the incident type, site, zone, and priority. Specificity: zone +4, site +2, priority limit +1; then compare bindingPriority. For different plans, publishing rejects intersections with the same specificity and priority. In each plan, the last published version is used. No match keeps the record `response.plan.missing` in audit and the event PlanUnavailable. For the type with responseRequired=true, the Incident Service adds a warning to the timeline; no run is created. The psim_response_plans_missing_total counter counts incidents without a match.

## Runs and Actions

`GET /v1/incidents/{id}/run` returns the run and the saved plan version. Reading is available to operator, supervisor, and auditor in the incident access scope. Changes are made through `POST /v1/runs/{runId}/steps/{stepId}:complete`, `:skip`, `:retry`, `:override`. A UUID is required in `Idempotency-Key` and the run version in `If-Match`. Repeating the same request returns the saved response; other content with the same key is rejected. Access is rechecked.

Complete confirms reading for instructions; for checklists, all required items are needed; for decisions, it takes result.decisionOptionId. Complete of the command step confirms sending but does not complete it. The client cannot substitute commandIds or overridden. Skipping a mandatory step is not allowed. Skip requires a reason, Retry is allowed after an error, Override is available to supervisor with a reason. Non-mandatory Decision is not skipped: the branch must be explicitly selected.

Each transition and change of mandatory steps are saved along with audit and outbox. Unselected branches remain uncompleted; their mandatory steps do not block incidents. Closing an incident cancels unfinished steps and timers. Actions already delivered to devices are not revoked.

## Commands and External Actions

The command step uses the Command Service through the durable CommandRequested. Explicit UUID of the device and `zone.devices` are supported, limited by site and incident zone, with a limit of 128 devices. The capability must be present in the active type of devices during publishing; before sending, the Command Service checks the actual device and parameters. Automatic steps initiate requests immediately; manual ones wait for Complete. Criticality is always determined by the catalog and requires separate confirmation from supervisor. Manual steps keep the verified user as the initiator of the command; self-confirmation is prohibited for supervisor. Automatic steps identify the service initiator. The step completes only after every command reaches EXECUTED. Any final failure, timeout, or UNKNOWN causes an error in the step. Retry creates a new attempt; the operator must consider already performed actions.

Notify persists the NotificationRequested and continues the graph; success of the step means persisting the intent, delivery through channels is done by [Notification Service](notifications.md). Escalate publishes EscalationLevelTriggered. Automated only calls the HTTPS address from handler_urls. Redirection is prohibited, response is limited to 64 KiB, timeout is 1-30 seconds. The service records the dispatch before the HTTP call and does not repeat it automatically after an unknown result. The request contains effectId, runId, incidentId, stepId, and tenantId. The handler can use effectId for its own idempotency.

## Configuration and Verification

Common parameters of Kafka, Schema Registry, PostgreSQL, and JWT/JWKS are used. HTTPS uses tls_certificate_file and tls_private_key_file or response-server.pem and response-server.key in tls_directory. handler_urls provides the exact list of allowed HTTPS addresses, handler_ca_file is the trusted CA for handlers. By default, the list is empty. The timer polling interval is 200 ms; the initial time and triggering are stored in response.timers. Metrics psim_response_transaction_seconds and psim_response_inputs_total show the processing of incident snapshots.

Verifications are performed through the native Docker Engine on Linux:

```
task response:test
task response:sanitizers
task response:coverage
task response:replay INPUT=path/to/replay.json
```

Artifacts: `build/response/<preset>/`, including reports, logs of two processes, and HTTPS receiver calls. The verification uses a separate Compose project; the name is set in [verification tasks](../../tools/taskfiles/response.yml). All processes are run with limited permissions. Replay verification mounts only the executable, sends the plan and history through stdin, and shuts down the network. Replay returns state, simulated actions, and externalCalls=0; it does not get credentials or change production data.

The INPUT file contains objects `plan` and `history`. `plan` is the complete ResponsePlan; `history` is a sequence of acknowledgments for instruction, checklist, and decision with fields `stepId`, `checkedItemIds`, and `decisionOptionId`. Asynchronous steps are performed through simulated adapters with successful results. Inappropriate order of acknowledgments is rejected.
