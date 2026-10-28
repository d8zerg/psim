# Documentation for Open-Source PSIM Platform

<!-- language-links -->
[English](README.md) | [Русский](ru/README.md)
<!-- /language-links -->

- [Implemented functions and release conditions](product/release-readiness.md)
- [Permanent quality requirements](quality/requirements.yaml)

## Product

- [Product Brief](product/product-brief.md)
- [Roles and user journeys](product/roles-and-journeys.md)
- [MVP scope](product/mvp-scope.md)
- [Operational capabilities of MVP](architecture/operational-capabilities.md)
- [S1-S13 acceptance specifications](product/acceptance-spec.md)

## Domain

- [Glossary](domain/glossary.md)
- [Bounded contexts and context map](domain/context-map.md)
- [Aggregates, invariants, domain events](domain/aggregates.md)
- [State models](domain/state-models.md)
- [Event taxonomy v1 and criticality model](domain/event-taxonomy.md)

- [End-to-end event flow](engineering/event-flow.md)
- [Connector development and conformance suite](engineering/connector-sdk.md)
- [Reference connectors: simulator and Webhook/HTTP](engineering/reference-connectors.md)
- [Normalizer and Event History](engineering/normalization-and-history.md)
- [Rules and historical runs](engineering/correlation-rules.md)
- [Live correlation and state restoration](engineering/live-correlation.md)
- [Resource Catalog: resources, floor plans and maintenance](engineering/resource-catalog.md)
- [Incident Service](engineering/incident-service.md)
- [Command Service: commands to devices](engineering/command-service.md)
- [Response Engine: scenario execution](engineering/response-engine.md)
- [SLA, duties, and notifications](engineering/notifications.md)
- [Audit Service and independent log verification](engineering/audit-service.md)
- [Incident reports](engineering/incident-reports.md)
- [Console projections: storage, freshness, and rebuilding](engineering/console-projections.md)
- [API Gateway: routing, configuration, and snapshots](engineering/api-gateway.md)
- [Searching event history and linking to incidents](engineering/event-history.md)
- [Connector Gateway: registration, sessions, and management](engineering/connector-gateway.md)

## Architecture

- [Architecture document arc42](architecture/README.md)
- [Runtime view](architecture/runtime-view.md)
- [Deployment and topology](architecture/deployment.md)
- [Cross-cutting concepts](architecture/crosscutting.md)
- [Data flows and delivery semantics](architecture/data-flows.md)
- [Scaling and partitioning](architecture/scaling.md)
- [Quality requirements and latency budgets](architecture/quality.md)
- [C4 model (PlantUML)](architecture/c4/L2-Containers.puml) - check: `task docs:c4`

## Architecture decisions

- [ADR registry](adr/README.md) · [template](adr/template.md)

## Contracts

- [Contract v1: contents, conventions, verification, scenario coverage](../contracts/README.md)
- [OpenAPI REST v1](../contracts/openapi/psim-api-v1.yaml) · [AsyncAPI topics](../contracts/asyncapi/psim-topics.yaml) · [proto reference guide](../contracts/docs/proto-reference.md)

## Security

- [Threat model: methodology, assets, trust boundaries, assumptions](security/threat-model.md)
- [Threat register and security requirements](security/threat-register.md) (generated from [threat-model.yaml](security/threat-model.yaml))

## Quality and verification

- [Quality and verification strategy](quality/strategy.md)
- [Verification matrix](quality/verification-matrix.md) (generated from [verification.yaml](quality/verification.yaml))
- [Methodology for cross-checking losses and duplicates](quality/reconciliation.md)
- [Latency measurement methodology](quality/latency-measurement.md)

## Engineering platform

- [C++23 coding standard](engineering/cpp-coding-standard.md)
- [TypeScript coding standard](engineering/typescript-coding-standard.md)
- Quality gates and fitness functions - [ADR-033](adr/0033-code-quality-gates.md); toolchain - [ADR-032](adr/0032-cpp-toolchain.md)
- Local CI/CD pipeline, packages, images, SBOM, and signatures - [ADR-034](adr/0034-local-ci-and-supply-chain.md)
- Documentation portal (`task docs:portal`, `task docs:serve`) - [ADR-038](adr/0038-documentation-portal.md)
- Design system and stable interface updates - [ADR-062](adr/0062-design-system-and-stable-updates.md), [guide](engineering/design-system.md), [extended scope and acceptance](engineering/design-system-acceptance.md)
- [UI terms ru/en](engineering/ui-glossary.md)
- Web monorepo (console, design system, REST clients, realtime) - [ADR-037](adr/0037-web-monorepo.md), [web/](../web/README.md)
- Local environment (Kafka, Schema Registry, PostgreSQL, ClickHouse, Valkey, Keycloak, monitoring, Toxiproxy) - [ADR-035](adr/0035-local-environment.md), [deploy/](../deploy/README.md)

## Platform core

- Service runtime: configuration, logs, metrics, tracing, health, shutdown with draining, service template - [ADR-039](adr/0039-service-runtime.md), [services](../services/README.md)
- Asynchronous model: shards, coroutines, deadlines, backpressure, blocking operation pool - [ADR-016](adr/0016-threading-model.md), [ADR-040](adr/0040-async-primitives.md)
- Kafka layer: producer, transactions, consumer, retry handler and DLQ, schema registry - [ADR-041](adr/0041-kafka-layer.md)
- Valkey/Redis layer: RESP3, Sentinel, cache, limits and sessions - [ADR-015](adr/0015-redis-client-and-role.md), [interfaces and examples](../libs/platform/redis/README.md)
- PostgreSQL layer: libpq, pools, transactions, migrations and inbox/outbox - [ADR-014](adr/0014-postgresql-async-driver.md), [interfaces and examples](../libs/platform/pg/README.md)

- Authorization, Keycloak, scope sets and current access validation - [ADR-061](adr/0061-live-access-validation.md), [guide](security/authorization.md)

- Platform security: TLS/mTLS, JWT/JWKS, secrets and external PKI - [ADR-043](adr/0043-platform-security.md), [interfaces](../libs/platform/security/README.md), [trust boundary](security/platform-trust.md)

- Realtime, Kafka recovery, and current state views - [ADR-060](adr/0060-realtime-delivery.md), [guide](engineering/realtime.md)

- [Server support for operational interface](engineering/operational-interface.md)
- [Application shell and session restoration](engineering/application-shell.md)
- [Administration](engineering/administration.md)
- [Size of administration assets](engineering/administration-assets.md)
- [Audit and reporting in the interface](engineering/audit-and-reporting.md)
- [Size of audit and reporting assets](engineering/audit-and-reporting-assets.md)

- [Web Client Qualification](engineering/web-client-quality.md)
- [Comparison of Web Client Loading](engineering/web-client-loading.md)
- [Delivery of two deployment topologies](engineering/deployment-topologies.md)
- [Performance qualification and sizing](engineering/performance.md)
- [Local KVM Lab](engineering/local-vm-lab.md)
- [Initial Trust and Accounts](engineering/deployment-pki.md)
- [Observability Pack and Dashboards](engineering/observability.md)

## Participation and Support

- [Participating in the Project](../CONTRIBUTING.md)
- [Security Issue Reports](../SECURITY.md)
- [API Compatibility, Connectors, and Release Support](engineering/compatibility-and-support.md)
