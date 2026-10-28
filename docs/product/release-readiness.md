# Implemented capabilities and release conditions

<!-- language-links -->
[English](release-readiness.md) | [Русский](../ru/product/release-readiness.md)
<!-- /language-links -->

This document describes available implementations and unverified release conditions for Open-Source PSIM Platform. Implementation or a successful component test does not establish an operational SLO. Criteria are defined in the [quality registry](../quality/requirements.yaml), [verification matrix](../quality/verification-matrix.md) and [acceptance scenarios](acceptance-spec.md).

## Implemented capabilities

| Area | Available implementation | Guide |
|---|---|---|
| Integration | C++ SDK, mTLS, disk journal, registration and session management; simulator and Webhook/HTTP | [Connectors](../engineering/connector-sdk.md), [reference implementations](../engineering/reference-connectors.md) |
| Catalog | Location hierarchy, sources, import, private PNG/JPEG floor-plan versions, one-time maintenance | [Resource Catalog](../engineering/resource-catalog.md) |
| Event processing | Normalization, deduplication, ClickHouse history, rules, windows, state recovery and isolated historical runs | [Normalization](../engineering/normalization-and-history.md), [rules](../engineering/correlation-rules.md) |
| Response | Incidents, response plans, commands with approvals, SLA, duty assignments, responsibility transfer and notifications | [Incidents](../engineering/incident-service.md), [response plans](../engineering/response-engine.md), [commands](../engineering/command-service.md), [notifications](../engineering/notifications.md) |
| Interface | Operator console, administration, audit and reports; access controls and session recovery | [Console](../engineering/operator-console.md), [administration](../engineering/administration.md), [audit and reports](../engineering/audit-and-reporting.md) |
| Data delivery | Persistent projections, realtime snapshots and deltas, distributed API quotas | [Projections](../engineering/console-projections.md), [realtime](../engineering/realtime.md), [API Gateway](../engineering/api-gateway.md) |
| Deployment | Debian 13 x86_64; containers and DEB; single machine and VM cluster; installation, rolling upgrade and rollback | [Topologies](../engineering/deployment-topologies.md), [installation and upgrades](../engineering/installation-and-upgrades.md) |
| Operations | C++ `psimctl`, independent encrypted backups, WAL, object and data recovery, DLQ, replay, diagnostics, metrics and alerts | [Recovery](../engineering/administration-and-recovery.md), [observability](../engineering/observability.md) |

## Mandatory unverified conditions

| Condition | Known result | Evidence required before claiming compliance |
|---|---|---|
| Full `LOAD-50K` profile | Component and local runs do not establish stability of the entire pipeline | 10 minutes of measured load at 50,000 events/s after warm-up, a 75,000 events/s peak for 5 minutes, end-to-end reconciliation and latency measurements for G-01-G-06 |
| Recovery and RPO/RTO | Recovery tools and isolated tests are implemented | Exercises in a clean target environment for both topologies, independent backup storage, measured RPO/RTO, and verification of permissions, keys and revoked certificates |
| Failure domains | A local VM cluster on one Linux host checks process and guest-OS failures | Verification of independent physical failure domains and the reference hardware configuration |
| Observability | Static checks and isolated Docker Compose verify queries, TLS, OTLP and alert delivery | Complete Grafana login through Keycloak, infrastructure protocols on the target cluster, an external recipient and independent `PsimWatchdog` monitoring |
| Security | Component permission, TLS and isolation tests and short fuzzing runs are available | Full security qualification: 24 hours per fuzz target, DAST, TLS verification and threat-model penetration testing |
| Documentation and demonstration | Complete paired EN/RU guides, diagrams, API references, dashboards and operational instructions; portal and extracted archive pass offline checks without development materials | Independent installation and connector integration using each edition, and a reproducible synthetic demonstration |

## Optional qualification

The following work is excluded from step 10.1 and does not block its acceptance or release. The performance report and sizing model remain required. An optional check that was not run does not establish compliance with its criteria.

| Work | Known result | Optional verification |
|---|---|---|
| S12 / C-12 upgrade | Sequential replacement and rollback are implemented; local checks found an approximately 19-second signal pause, incident lag and an SDK buffer error during ramp-up | Resolve causes and complete an upgrade at target load with a pause of at most 5 seconds, SLO compliance and full reconciliation |
| Profiling, optimization and persistent-disk correlation | The correlation benchmark used tmpfs and a limited key count | Profile hot paths, optimize based on results and measure persistent-disk state with the reference key count |
| Sizing validation on two configurations | The sizing model is prepared using available measurements and extrapolation | Validate the model on at least two test-environment configurations |

## Release-note rules

Release notes must identify the version, supported N -> N+1 upgrade pair, migrations, configuration changes, connector compatibility, upgrade order, rollback conditions and known limitations. Provide a result and reproducible verification evidence for every mandatory criterion. Explicitly identify tests that were not run or did not pass; results from a different profile cannot substitute for them.

Before publication, verify artifact signatures, SBOM and availability of instructions without external network access. Support and public-contract change rules are defined in the [compatibility policy](../engineering/compatibility-and-support.md). This document does not declare the MVP release accepted.
