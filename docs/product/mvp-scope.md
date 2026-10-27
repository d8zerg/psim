# MVP Boundaries

<!-- language-links -->
[English](mvp-scope.md) | [Русский](../ru/product/mvp-scope.md)
<!-- /language-links -->

Each function has a stable identifier `F-<context>-<number>` and references to acceptance scenarios. Contexts are defined in the [domain diagram](../domain/context-map.md).

---

## 1. MVP Functions

### 1.1 Integration - Connectors and Event Input

| ID | Function | Scenarios |
|---|---|---|
| F-INT-01 | gRPC protocol for connectors: registration, heartbeat, event flow with acknowledgments, flow management based on credits, commands and results, configuration transfer | S1, S8 |
| F-INT-02 | Connector SDK for C++: disk buffer with replay and retries, command reception, metrics | S1, S11 |
| F-INT-03 | Conformance kit for testing protocol compliance | S1 |
| F-INT-04 | Connector Gateway: mTLS, authentication, schema validation, connector limits, acknowledgment after writing to Kafka, horizontal scaling | S1, S3, S11 |
| F-INT-05 | Simulator and load generator: thousands of virtual devices, profiles, peaks, sequence numbers for comparison, command reception | S3-S11 |
| F-INT-06 | Standard Webhook/HTTP connector for receiving events from external systems | S1 |
| F-INT-07 | Developer guide for connectors | S1 |
| F-INT-08 | Current state reconciliation after connection, device availability and data quality; historical replay does not make state current | S6, S11 |

### 1.2 Catalog - Objects and Devices

| ID | Function | Scenarios |
|---|---|---|
| F-CAT-01 | Hierarchy: site -> building -> floor -> zone | S2 |
| F-CAT-02 | Devices, sources, types, capabilities (including supported commands), binding to zones and connectors | S2, S8 |
| F-CAT-03 | Mass import and export (CSV, JSON) with row-by-row reporting | S2 |
| F-CAT-04 | Floor plans with device coordinates, version in file storage | S2 |
| F-CAT-05 | Publishing changes to `psim.catalog.v1` (compacted) | S2, S3 |
| F-CAT-06 | One-time maintenance for devices or zones: duration, reason, responsible person, explicit suppression, automatic termination, event and audit storage | S4, S10 |

### 1.3 Event Processing - Normalization and Correlation

| ID | Function | Scenarios |
|---|---|---|
| F-EVP-01 | v1 event taxonomy: security, fire, access control, video analytics, perimeter, technical failures; severity model | S3 |
| F-EVP-02 | Declarative rules for mapping raw events in the taxonomy | S1, S3 |
| F-EVP-03 | Enrichment of the site, zone, type, and severity from local catalog copy | S3 |
| F-EVP-04 | Deduplication by source sequence number; rejection in DLQ with reason | S3, S11 |
| F-EVP-05 | Event History in ClickHouse: storage of all events (significant ones - longer), search by object, zone, device, type, time (ADR-028) | S7 |
| F-EVP-06 | Correlation rule language: conditions, windows, sequences, thresholds, absence of event, multiple source connection, suppression, priority calculation | S4 |
| F-EVP-07 | Lifecycle of rules: draft -> test -> active -> archive; validation; trial run on history | S4 |
| F-EVP-08 | Correlation Engine: state in RocksDB with changelog, event time and watermarks, exactly-once, atomic rule version switching | S4, S5, S11 |

### 1.4 Incident Management

| ID | Function | Scenarios |
|---|---|---|
| F-INC-01 | Lifecycle: new -> acknowledged -> in progress -> resolved -> closed; escalation - separate indicator, false alarm - result of resolution | S5-S7 |
| F-INC-02 | Grouping signals into open incidents by key and window; zero duplicates | S5 |
| F-INC-03 | Priorities and their recalculation | S5, S6 |
| F-INC-04 | Assignment and transfer between operators | S7, S9 |
| F-INC-05 | Chronology, comments, linked attachments | S7 |
| F-INC-06 | Links between incidents, signals and source events | S7 |
| F-INC-07 | Transfer of unclosed incidents with acknowledgment of the recipient, verification of version and access scope, preservation of SLA and scenario | S7, S10 |

### 1.5 Response - Reaction, Commands, Escalations, Notifications

| ID | Function | Scenarios |
|---|---|---|
| F-RSP-01 | Response scenarios: instruction, checklist, branching decision, command, notification, waiting, escalation, mandatory step | S7 |
| F-RSP-02 | Versioning of scenarios; version is pinned at run start | S7 |
| F-RSP-03 | Binding scenarios to incident types, zones, priorities | S7 |
| F-RSP-04 | Reliable execution with continuation after restart; timer service with accuracy ≤ 1 sec | S7, S9 |
| F-RSP-05 | Commands to devices: lifecycle, authorization, confirmation of critical commands, idempotence, timeouts | S8 |
| F-RSP-06 | SLA policies for acceptance and resolution, multi-level escalations | S9 |
| F-RSP-07 | Notifications: in the application, email, webhook; retries, delivery tracking, suppression during mass events | S9 |
| F-RSP-08 | Explicit assignment of the current site duty; escalation recipient resolution and backup contact | S9, S10 |

### 1.6 Audit and Reporting

| ID | Function | Scenarios |
|---|---|---|
| F-AUD-01 | Unchangeable log on the hash chain with checkpoints | S10 |
| F-AUD-02 | Integrity checking, search, export | S10 |
| F-AUD-03 | Audit of all user and system actions, including access failures and administrator actions | S10 |
| F-REP-01 | Reports: number of incidents by type and object, MTTA, MTTR, false alarm ratio, operator load, SLA compliance; CSV export | S13 |

### 1.7 Identity & Access, API

| ID | Function | Scenarios |
|---|---|---|
| F-IAM-01 | Login through Keycloak (OIDC, PKCE) | All UI |
| F-IAM-02 | RBAC by roles from [roles-and-journeys.md](roles-and-journeys.md), ABAC by sites and zones | S6-S10, S13 |
| F-IAM-03 | Connector accounts (mTLS) | S1 |
| F-API-01 | REST API v1: cursor pagination, RFC 9457 errors, idempotence keys, ETag | All |
| F-API-02 | Real-time WebSocket channel: snapshots and changes of incidents, scenarios, devices, zones and connectors; filters by access scope, token-based resumption | S6, S11 |
| F-API-03 | Read models: incident feed, status of objects and devices with quality and time of confirmation, operator workload; rebuild from Kafka | S6 |

### 1.8 Web Application

| ID | Function | Scenarios |
|---|---|---|
| F-WEB-01 | Design system: dark and light themes, density for operators, ru and en, accessibility | All UI |
| F-WEB-02 | Shell: entry, role modules, communication indication, recovery after disconnection | S6, S11 |
| F-WEB-03 | Operator Console: feed with up to 10,000 incidents, card, scenario, commands, overview of objects on the floor plan, hotkeys, audio notifications, status freshness, responsibility transfer and current duty | S6-S9 |
| F-WEB-04 | Administration: catalog and import, rule editor with trial run, scenario editor in form, connectors and certificates, maintenance management, roles and access scopes, retention | S1, S2, S4 |
| F-WEB-05 | Audit and reports with charts and export | S10, S13 |

### 1.9 Platform - Deployment and Operation

| ID | Function | Scenarios |
|---|---|---|
| F-PLT-01 | Topology: single machine, cluster on VM | S11, S12 |
| F-PLT-02 | Installer, expand/contract migrations, zero-downtime updates, rollback | S12 |
| F-PLT-03 | Backup and restoration: PITR, configuration, compacted topics | - |
| F-PLT-04 | Observability: OTLP tracing, Prometheus metrics, Grafana dashboards, alerts with runbook | S11 |
| F-PLT-05 | CLI for administrator: diagnosis, re-processing of DLQ, rebuilding projections, event playback, audit verification | S10, S11 |
| F-PLT-06 | Supply: packages for Debian 13 x86_64, container images without root, SBOM, signatures | S12 |
| F-PLT-07 | Floor plan storage through FileStore and S3-compatible adapter: versions, authorization, upload validation, backup | S2, S11 |
| F-PLT-08 | Registration, renewal, revocation of certificates, restoration of trust and Keycloak; external PKI, procedures for both topologies | S1, S11 |
| F-PLT-09 | Isolated diagnostic replay without production incidents, commands, notifications, and external calls | S4, S8, S10 |

---

## 2. Beyond MVP

Explicit list. Changes require product owner and ADR decision.

| No. | Beyond MVP | What is included in MVP instead |
|---|---|---|
| X1 | AI: automatic sorting of alarms, reducing false triggers and model-based actions | Assistant for reading permitted facts from sources via the operational interface (ADR-067); other capabilities use ADR-025 extensions |
| X2 | GIS and BIM | Floor plan images with coordinates |
| X3 | Mobile app for field teams | Email and webhook notifications |
| X4 | Federation of situational centers and multi-tenant operation | tenant_id in data models, contracts, and keys; one tenant |
| X5 | Edge nodes on objects | Disk buffering in Connector SDK |
| X6 | Telephony, radio communication, mass notification | "Notification" scenario step and webhook |
| X7 | Video wall; built-in video viewing | Links to external resources in incident attachments |
| X8 | BI tools and arbitrary analytics | Event and incident facts in ClickHouse (ADR-028), MVP reports, CSV export |
| X9 | Connectors to specific vendor products beyond the standard ones | Connector SDK and test suite for partners |
| X10 | Geographically distributed failover | Cluster in one data center with RTO ≤ 60 seconds |
| X11 | Certifications | Distribution choices will be reviewed later |
| X12 | Connectors in languages other than C++ | gRPC protocol allows writing connectors in any language without SDK |
| X13 | Scripting languages in rules and scenarios | Declarative models of rules and scenarios (ADR-011) |
| X14 | Separate versions and licensing of functions | One version for Enterprise |
| X15 | Kubernetes topology (Helm chart, operators) | Single machine and cluster on VM; container images are compatible with Kubernetes |
| X16 | Standard connectors beyond simulator and Webhook/HTTP, including MQTT and ONVIF | Protocol and SDK open to partners |
| X17 | Visual editor for scenario editing | Form-based scenario editor |
| X18 | Own storage for incident photos, documents, and videos | File storage only for plans; incident attachments are external systems, links |
| X19 | Shift calendars, holidays, automatic replacements, and recurring maintenance | Explicit duty assignment, confirmed incident transfer, and one-time maintenance intervals |

---

## 3. Accepted scope reductions

For development by one engineer with AI assistants, the following scope reductions are accepted. Functions F-INT-06, F-PLT-01, and F-WEB-04 have explicitly limited content; excluded capabilities are listed as X15-X17.

| Reduction | Affected functions | Impact on acceptance |
|---|---|---|
| Visual editor for scenario editing is replaced with form | F-WEB-04 | No: S7 is executed |
| From standard connectors, simulator and Webhook/HTTP remain | F-INT-06 | No: S1 is checked by one connector |
| Kubernetes is moved beyond MVP | F-PLT-01 | No: S11, S12 are checked on VM cluster |

## 4. Accepted operational additions

Accepted: minimum scope from [operational-capabilities.md](../architecture/operational-capabilities.md), [ADR-042](../adr/0042-operational-capabilities.md). Priority - state reliability and realtime, file storage and recovery, then maintenance management, certificates and replay, minimum responsibility transfer and duty. Implemented functions and unconfirmed conditions are listed in [release readiness](release-readiness.md).

Rules of participation and vulnerability reporting are published in [CONTRIBUTING.md](../../CONTRIBUTING.md) and [SECURITY.md](../../SECURITY.md). Compatibility and support are defined in [policy](../engineering/compatibility-and-support.md); reproducible synthetic demonstration remains a required condition for acceptance.
