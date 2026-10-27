# Open-Source PSIM Platform v1 Contracts

<!-- language-links -->
[English](README.md) | [Русский](../docs/ru/repository/contracts/README.md)
<!-- /language-links -->

The public versioned contracts of the platform are the sole source of truth for code, client generation, and documentation (ADR-001, ADR-004, ADR-026).

## Composition

| Category | What | Source or Generated |
|---|---|---|
| [proto/psim/common/v1](proto/psim/common/v1) | Message container, common enumerations (ADR-005) | source |
| [proto/psim/ingest/v1](proto/psim/ingest/v1) | Raw event, topic `psim.ingest.raw.v1` | source |
| [proto/psim/processing/v1](proto/psim/processing/v1) | Normalized event, signals, correlation and mapping rules, internal topics | source |
| [proto/psim/catalog/v1](proto/psim/catalog/v1) | Locations, device types, devices, sources, connectors | source |
| [proto/psim/incident/v1](proto/psim/incident/v1) | Incident, incident type, chronology, incident events | source |
| [proto/psim/response/v1](proto/psim/response/v1) | Scenarios, executions, commands, escalation, notifications, their events and topic | source |
| [proto/psim/config/v1](proto/psim/config/v1) | Configuration topic records | source |
| [proto/psim/audit/v1](proto/psim/audit/v1) | Audit records and checkpoints | source |
| [proto/psim/connector/v1](proto/psim/connector/v1) | Connector protocol, gRPC (ADR-017) | source |
| [proto/psim/realtime/v1](proto/psim/realtime/v1) | Real-time channel protocol, WebSocket (ADR-019) | source |
| [proto/psim/api/v1](proto/psim/api/v1) | REST API v1: services with `google.api.http` (ADR-026) | source |
| [openapi/overlay.yaml](openapi/overlay.yaml) | What is not expressed in annotations: security, `If-Match`, `Idempotency-Key`, RFC 9457, binary bodies | source |
| [openapi/psim-api-v1.yaml](openapi/psim-api-v1.yaml) | OpenAPI 3 REST API v1 | generated |
| [topics/topics.yaml](topics/topics.yaml) | Kafka topics registry: keys, partitions, retention, semantics, producers and consumers | source |
| [asyncapi/psim-topics.yaml](asyncapi/psim-topics.yaml) | AsyncAPI 3 Kafka topics | generated |
| [taxonomy/v1/taxonomy.yaml](taxonomy/v1/taxonomy.yaml) | Event taxonomy v1 | source |
| [errors/errors.yaml](errors/errors.yaml) | Code catalog of errors | source |
| [docs/proto-reference.md](docs/proto-reference.md) | Reference guide for all messages and services | generated |

Generated files are stored in the repository so they can be read and reviewed, but are only managed through sources.

## Conventions

- Packages `psim.<context>.v<N>`, files `proto/psim/<context>/v<N>/*.proto`; buf rules `STANDARD` + `COMMENTS`.
- Message value in a topic is a record `<...>Record { Envelope envelope = 1; oneof payload {...} }`; the suffix `Record` is reserved for topic records.
- Aggregate events carry the aggregate state after the change; `envelope.sequence` equals its version. Consumers apply the event only if the version is higher than the applied one.
- Identifiers are UUID strings; construction rules - ADR-005 and ADR-029.
- Field restrictions - annotations protovalidate (`buf.validate`); services verify incoming messages with the same rules.
- REST: canonical Protobuf JSON mapping, cursor pagination, `If-Match` for versioned resource changes, `Idempotency-Key` for non-idempotent operations, errors `application/problem+json`.

## Evolution Rules (ADR-004)

1. Only adding optional fields with new numbers; deleted fields and numbers - in `reserved`.
2. The type and meaning of a field do not change; new meaning - new field.
3. New event type - new branch `oneof`; consumers ignore unknown branches.
4. Incompatible changes - new package `v2` and new topic `.v2`.
5. Taxonomy and error catalog - only adding; codes are not renamed or reused.

Automation: `buf breaking` (rules `WIRE_JSON`) against branch `master`; in Schema Registry - compatibility modes from ADR-004.

## Verification

All actions are performed by [Tasks](https://taskfile.dev) ([ADR-030](../docs/adr/0030-task-runner.md)):

```
task contracts:check     # Check everything; fail if generated files are stale
task contracts:gen       # Regenerate OpenAPI, AsyncAPI and the reference; check consistency
task contracts:lint      # Individual tasks: format, format:fix, lint, breaking, build, openapi, asyncapi, reference, verify
task contracts:buf -- dep update   # Run an arbitrary buf command
```

What is verified:

| Verification | Tool |
|---|---|
| Formatting, linting, proto build | buf 1.73 |
| Backward compatibility against `master` | `buf breaking` |
| OpenAPI: generation, overlay, validity | protoc-gen-openapi, `tools/contracts/openapi_overlay.py`, Redocly CLI |
| AsyncAPI: generation from registry, validity | `tools/contracts/contracts.py`, AsyncAPI CLI |
| Proto reference is up-to-date | protoc-gen-doc |
| Topics registry: records exist in proto, have container and `oneof payload`; each record is linked to a topic; matches table in [data-flows.md](../docs/architecture/data-flows.md#4-summary-table) | `contracts.py verify` |
| Taxonomy: uniqueness, format, classes, criticality, references to restoration; matches [event-taxonomy.md](../docs/domain/event-taxonomy.md) | `contracts.py verify` |
| Error catalog: format, HTTP and gRPC status; all codes mentioned in documentation and proto are in the catalog | `contracts.py verify` |
| The `Problem` schema in the overlay matches `psim.api.v1.Problem` | `contracts.py verify` |

Tools are run in containers with fixed versions; Docker, Python 3 with PyYAML and Task are required. Buf and the plugins `protoc-gen-openapi`, `protoc-gen-doc` are built into the local image ([tools/contracts/image](../tools/contracts/image/Dockerfile), task `task tools:build`), so verifications do not depend on Buf Schema Registry limits and work without a network after the first dependency load. The local CI verifies contracts as part of `task ci`.

## Coverage of Acceptance Scenarios

Each scenario S1-S13 ([acceptance-spec.md](../docs/product/acceptance-spec.md)) is expressed through contracts:

| Scenario | Contracts |
|---|---|
| S1 Connector integration | `CatalogService.RegisterConnector`; `ConnectorGatewayService.Session` (`Register`, `Registered`, `EventBatch`, `BatchAck`, `ConfigUpdate`); `MappingService.PublishMappingRuleSet`; `CatalogService.ListConnectors` (`ConnectorStatus.online`); topics `psim.ingest.raw.v1`, `psim.events.normalized.v1`, `psim.dlq.normalizer.v1`; codes `AUTH_UNAUTHENTICATED`, `INGEST_UNKNOWN_CONNECTOR`, `INGEST_INVALID_EVENT`, `PROCESSING_NO_MAPPING` |
| S2 Site configuration | `CatalogService`: `CreateLocation`, `UploadFloorPlan`, `StartImport`, `GetImport` (`ImportJob.errors`), `UpdateDevice` (`position`); topic `psim.catalog.v1`; codes `CATALOG_DUPLICATE_EXTERNAL_ID`, `CATALOG_IMPORT_ROW_INVALID` |
| S3 Normalization | `RawEvent` -> `Event` (taxonomy type, `severity`, `site_id`, `zone_id`, `device_id`); `Envelope.sequence`, `traceparent`; `SourceSequenceState` (duplication elimination); codes `PROCESSING_DEVICE_NOT_FOUND`, `PROCESSING_DEVICE_DISABLED` |
| S4 Correlation | `CorrelationRule` (`ConjunctionSpec`, `window`, `correlation_scope`); `CorrelationRuleService`: `SaveRuleDraft`, `ValidateRule`, `StartDryRun`, `ActivateRuleSet`; `ConfigRecord.rule_set`; `Signal` (`event_ids`, `origin`); code `PROCESSING_RULE_INVALID` |
| S5 Incident deduplication | `Signal.grouping_key`, deterministic signal `Envelope.message_id`; `IncidentCreated`, `SignalAttached` |
| S6 Incident in the console | `FeedService.GetIncidentFeed` (`resume_token`); realtime `Subscribe`, `Delta`; `Envelope.occurred_at` for latency measurement; code `AUTH_SCOPE_DENIED` |
| S7 Scenario | `IncidentService.AcknowledgeIncident`, `ResolveIncident` (`known_response_run_version`); `ResponsePlan` (`ChecklistParams`, `DecisionParams`, `Step.mandatory`); `ResponseRunService.CompleteStep`; `RunUpdated`; code `INCIDENT_MANDATORY_STEPS_PENDING` |
| S8 Command | `CommandService.RequestCommand`, `ApproveCommand`; `CommandDeliveryRecord`; protocol `Command`, `CommandAck`, `CommandResult`; `CommandUpdated`; codes `COMMAND_APPROVAL_SELF`, `COMMAND_CONNECTOR_OFFLINE`, `COMMAND_TIMED_OUT`, `COMMAND_DEVICE_REJECTED` |
| S9 Escalation | `EscalationPolicy` (`SlaTarget`, `EscalationLevel`); `SlaBreached`, `EscalationLevelTriggered`, `IncidentEscalated`; `NotificationUpdated`; `NotificationService.ListMyNotifications` |
| S10 Audit | `AuditEventRecord` (`AuditEntry`, `AuditCheckpoint`); `AuditService`: `SearchAuditRecords`, `VerifyAuditIntegrity` (`first_broken_seq`), `ExportAuditRecords`; `AuditOutcome.AUDIT_OUTCOME_DENIED` |
| S11 Failure without data loss | Semantics of topics in [topics.yaml](topics/topics.yaml); `ConnectorEvent.event_id`, `source_seq`; `BatchAck`; realtime `resume_token`, `ResyncRequired`; code `REALTIME_RESYNC_REQUIRED` |
| S12 Update | Rules for evolution and `buf breaking`; version synchronization in `Register.protocol_versions`; `Envelope.schema_version` |
| S13 Report | `ReportService.GetIncidentReport` (`IncidentMetrics`: `mtta`, `mttr`, `false_alarm_rate`, SLA compliance), `ExportIncidentReport` (CSV) |

Contract upgrade mechanisms are implemented; a successful S12 load scenario is required before release. Unverified criteria are listed in [release readiness](../docs/product/release-readiness.md).
