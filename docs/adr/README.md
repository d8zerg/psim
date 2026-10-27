# Architecture decision records (ADR)

<!-- language-links -->
[English](README.md) | [Русский](../ru/adr/README.md)
<!-- /language-links -->

Use the MADR-based [template](template.md).

## Statuses

| Status | Meaning |
|---|---|
| Proposed | Defined but not accepted; approve before implementation using measurements and prototypes |
| Accepted | In force; implementation must follow the decision |
| Superseded by ADR-NNN | Replaced by a later ADR |
| Rejected | Considered and not accepted |

Accept an ADR before implementing changes that depend on it. A substantive change to an accepted decision requires a new ADR that supersedes the old one; do not rewrite the old decision.

## Registry

| ID | Decision | Status |
|---|---|---|
| [ADR-001](0001-monorepo-structure.md) | Monorepo structure and module boundaries | Accepted |
| [ADR-002](0002-kafka-central-bus.md) | Kafka as the central bus: distribution and version | Accepted |
| [ADR-003](0003-schema-registry.md) | Schema Registry | Accepted |
| [ADR-004](0004-serialization-and-schema-evolution.md) | Serialization format and schema evolution rules | Accepted |
| [ADR-005](0005-envelope-and-identifiers.md) | Message envelope, UUIDv7 and named UUIDs | Accepted |
| [ADR-006](0006-delivery-semantics.md) | Delivery semantics by data flow | Accepted |
| [ADR-007](0007-partitioning-strategy.md) | Partitioning strategy and high-load keys | Accepted |
| [ADR-008](0008-cqrs-outbox-inbox.md) | CQRS, transactional outbox and inbox | Accepted |
| [ADR-009](0009-correlation-state-store.md) | Correlation state storage: RocksDB with a changelog topic | Accepted |
| [ADR-010](0010-time-semantics.md) | Time semantics: event time, watermarks and clock skew | Accepted |
| [ADR-011](0011-correlation-rule-language.md) | Correlation rule language | Proposed |
| [ADR-012](0012-response-execution-and-timers.md) | Response-plan execution and timers | Accepted |
| [ADR-013](0013-event-history-storage.md) | MVP event-history storage and long-term archive | Superseded by ADR-028 |
| [ADR-014](0014-postgresql-async-driver.md) | Asynchronous PostgreSQL driver | Accepted |
| [ADR-015](0015-redis-client-and-role.md) | Redis-compatible store, client and responsibilities | Accepted |
| [ADR-016](0016-threading-model.md) | Execution threads and coroutines | Accepted |
| [ADR-017](0017-connector-protocol.md) | Connector protocol and flow control | Accepted |
| [ADR-018](0018-command-routing.md) | Routing commands to connector sessions | Accepted |
| [ADR-019](0019-realtime-channel.md) | Realtime channel: WebSocket, snapshots, deltas and offset-based resumption | Accepted |
| [ADR-020](0020-authorization-model.md) | Authorization: RBAC, ABAC and the policy decision point | Accepted |
| [ADR-021](0021-audit-hash-chain.md) | Tamper-evident audit hash chain | Accepted |
| [ADR-022](0022-multitenancy.md) | Multitenant data model with one tenant in MVP | Accepted |
| [ADR-023](0023-deployment-topologies.md) | Deployment topologies and single-machine reliability limits | Accepted |
| [ADR-024](0024-zero-downtime-upgrades.md) | Zero-downtime upgrades and migrations | Accepted |
| [ADR-025](0025-ai-extension-points.md) | AI extension points | Accepted |
| [ADR-026](0026-rest-contract-and-dto.md) | REST contracts and DTOs: proto-first OpenAPI generation | Accepted |
| [ADR-027](0027-incident-states.md) | Escalation and false alarms are not incident states | Accepted |
| [ADR-028](0028-clickhouse-analytical-storage.md) | ClickHouse for event history and analytics | Accepted |
| [ADR-029](0029-deterministic-source-id.md) | Deterministic source identifiers | Accepted |
| [ADR-030](0030-task-runner.md) | Task as the development task entry point | Accepted |
| [ADR-031](0031-verification-tooling.md) | Verification tools | Accepted |
| [ADR-032](0032-cpp-toolchain.md) | C++ toolchain: Debian 13 x86_64, Clang 21, libstdc++, Conan 2 and CMake presets | Accepted |
| [ADR-033](0033-code-quality-gates.md) | Code quality gates: formatting, clang-tidy, includes, sanitizers, coverage and fitness functions | Accepted |
| [ADR-034](0034-local-ci-and-supply-chain.md) | Local CI/CD and software supply chain | Accepted |
| [ADR-035](0035-local-environment.md) | Local development and integration-test environment | Accepted |
| [ADR-036](0036-no-git-hooks.md) | Remove Git hooks and run `task ci` before committing | Accepted |
| [ADR-037](0037-web-monorepo.md) | Web monorepo: tools, client generation and quality gates | Accepted |
| [ADR-038](0038-documentation-portal.md) | Documentation portal built from the repository | Accepted |
| [ADR-039](0039-service-runtime.md) | Service Runtime | Accepted |
| [ADR-040](0040-async-primitives.md) | Asynchronous primitives of the platform | Accepted |
| [ADR-041](0041-kafka-layer.md) | Kafka layer | Accepted |
| [ADR-042](0042-operational-capabilities.md) | Operational capabilities within MVP scope | Accepted |
| [ADR-043](0043-platform-security.md) | Platform Security: TLS, JWT/JWKS, Secrets, and External PKI | Accepted |
| [ADR-044](0044-deterministic-simulation.md) | Deterministic Simulation Environment with Manual Execution | Accepted |
| [ADR-045](0045-clickhouse-layer.md) | Native ClickHouse layer and Kafka batch recovery | Accepted |
| [ADR-046](0046-s3-object-storage.md) | S3-compatible file storage | Accepted |
| [ADR-047](0047-event-flow.md) | Minimal end-to-end event flow | Accepted |
| [ADR-048](0048-connector-sdk.md) | Connector SDK, disk buffer and state reconciliation | Accepted |
| [ADR-049](0049-connector-gateway.md) | Connector Gateway: Registration, Sessions, and Revocation Confirmation | Accepted |
| [ADR-050](0050-reference-connectors.md) | Reference connectors and durable HTTP retries | Accepted |
| [ADR-051](0051-resource-catalog.md) | Resource Catalog: Resources, files and maintenance | Accepted |
| [ADR-052](0052-normalization-and-event-history.md) | Normalization, Source Cursors, and Event History | Accepted |
| [ADR-053](0053-incident-service.md) | Incident Service | Accepted |
| [ADR-054](0054-event-history-links.md) | Event History, links and extended retention | Accepted |
| [ADR-055](0055-command-service.md) | Device Command Service | Accepted |
| [ADR-056](0056-sla-and-notifications.md) | SLA, duty assignment, and Notification Delivery | Accepted |
| [ADR-057](0057-incident-reporting.md) | Incident Facts and Reporting | Accepted |
| [ADR-058](0058-api-gateway-and-configuration.md) | API Gateway and Configuration Management for Normalization | Accepted |
| [ADR-059](0059-console-projections.md) | Console read models in Projection Service | Accepted |
| [ADR-060](0060-realtime-delivery.md) | Realtime resume from Kafka and operational snapshots | Accepted |
| [ADR-061](0061-live-access-validation.md) | Checking current token validity and scope sets | Accepted |
| [ADR-062](0062-design-system-and-stable-updates.md) | Design System and Stable Interface Updates | Accepted |
| [ADR-063](0063-interface-principles.md) | Principles of Real-Time Interface | Accepted |
| [ADR-064](0064-alarm-philosophy.md) | Philosophy of Operator Alarms and Load | Accepted |
| [ADR-065](0065-ui-loading-budgets.md) | Module Loading and Budgets | Accepted |
| [ADR-066](0066-ui-font-and-icons.md) | Local Fonts and Object Icons | Accepted |
| [ADR-067](0067-assistant-boundaries.md) | Assistant with sources and Without Command Execution | Accepted |
| [ADR-068](0068-operational-interface-backend.md) | Server Operations for the Real-time Interface | Accepted |
| [ADR-069](0069-application-shell-and-session-recovery.md) | Application Shell and Session Recovery | Accepted |
| [ADR-070](0070-lazy-loading-and-delivery-budgets.md) | Lazy module loading and Delivery Budgets | Accepted |
| [ADR-071](0071-operator-console.md) | Operator’s Work Console | Accepted |
| [ADR-072](0072-administration.md) | Administration Module | Accepted |
| [ADR-073](0073-administration-delivery-budget.md) | Budget for the entire application graph with administration | Accepted |
| [ADR-074](0074-audit-and-reporting-interface.md) | Audit and Reporting Interface | Accepted |
| [ADR-075](0075-audit-and-reporting-delivery-budget.md) | Budget for a unique JS delivery with auditing and reporting | Accepted |
| [ADR-076](0076-web-client-qualification.md) | Web Client Qualification | Accepted |
| [ADR-077](0077-seaweedfs-deployment-topology.md) | Operational Topology of SeaweedFS | Accepted |
| [ADR-078](0078-shared-api-rate-limits.md) | Shared quotas for REST API in Valkey | Accepted |
| [ADR-079](0079-release-version-identification.md) | Defining Version and Identity of a Delivery | Accepted |
| [ADR-080](0080-observability-package.md) | Telemetry storage and observability package | Accepted |
| [ADR-081](0081-administration-and-recovery.md) | Native Administrative Operations and Backup Copies | Accepted |

ADR-042 defines operational requirements for files, state, maintenance, responsibility handover, certificates and replay.
