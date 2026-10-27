# Open-Source PSIM Platform - Product Brief

<!-- language-links -->
[English](product-brief.md) | [Русский](../ru/product/product-brief.md)
<!-- /language-links -->

Related documents: [Roles and User Paths](roles-and-journeys.md) · [MVP Boundaries](mvp-scope.md) · [Acceptance Criteria](acceptance-spec.md) · [Release Conditions](release-readiness.md)

This brief defines product targets. Implemented capabilities, completed checks and outstanding qualification are recorded in [release readiness](release-readiness.md).

---

## 1. Vision

Open-Source PSIM Platform is a vendor-neutral platform for managing physical security situations. It collects events from any security and engineering infrastructure systems into a single stream, converts them into incidents, and guides operators through a regulated response process until closure and reporting.

> An event from any source can become an incident within one second with a clear action plan, and each operator’s action is documented and verifiable.

## 2. Problem

| Observation | Impact on Customer |
|---|---|
| Different manufacturers' systems are operating on the asset: security and fire alarm systems, CCTV, video analytics, perimeter surveillance, engineering systems. Each has its own console | Operators switch between screens and miss connections between events |
| Existing PSIMs are tied to their manufacturer's equipment or require core modifications for each integration | High cost and integration time, dependency on the vendor |
| Response instructions are stored in paper form or files | Response depends on the operator’s experience, violations go unnoticed |
| Thousands of events per minute during mass reactions | Overloading of operators, false alarms mask real incidents |
| Operator actions are not recorded or are recorded in a mutable log | It is impossible to analyze incidents and investigate evidence |

## 3. Positioning

**For** security services and system integrators who need to manage situations on assets with equipment from different manufacturers,
**Open-Source PSIM Platform** is an open-source platform for situation management under MIT license,
**which** connects any source through an open connector protocol, correlates events in real time, and guides operators through executable response scenarios.
**Unlike** solutions tied to manufacturer equipment and ad-hoc integrations,
**the platform** is independent of the vendor, scales from a single machine to a cluster with the same delivery artifacts, and reliably retains events and incidents.

Project Definitions:

| Purpose | Definition |
|---|---|
| Public product name | Open-Source PSIM Platform |
| Repository, C++/Protobuf namespaces, topic prefixes, and SDK packages | `psim` |
| Technical name for the test environment, build images, and delivery | `psim-platform` |

## 4. Target Customers

| Segment | Need | Role in MVP |
|---|---|---|
| Large organizations with distributed assets (industry, energy, transportation, campuses) | Unified situation center, regulations, reporting | Main acceptance segment |
| System integrators | Quickly connect customer equipment without core modifications | Connector SDK users |
| Security service operators and monitoring centers | High load, SLA response, operator control | Load testing scenarios |

## 5. Value Propositions

1. **Vendor neutrality.** Open connector protocol, SDK, and conformance test suite. New sources are connected without core changes (S1).
2. **Real-time correlation.** Events from multiple sources are linked by rules in the time window. The operator sees an incident, not a stream of alarms (S4, S5).
3. **Executable scenarios.** Response procedure is executed step by step: checklists, decisions, mandatory actions, device commands (S7, S8).
4. **Guaranteed response.** SLA for acknowledgement and resolution, automatic escalation (S9).
5. **Verifiability.** Tamper-evident audit backed by a hash chain, MTTA and MTTR reports (S10, S13).
6. **Reliability and scalability.** 50,000 events/second, zero loss and duplicates, continuous updates, one delivery for any topology (S11, S12).

## 6. Unique Features

| Feature | How Verified |
|---|---|
| Zero loss and duplicates in failures | Deterministic simulation testing with fault insertion and chaos tests with cross-checking |
| Event -> Incident in the console within p99 ≤ 1s | Event time measurements in load tests |
| Connection of connectors without core changes | Third-party developers write connectors according to documentation and pass conformance tests |
| Reproducibility | Any read model is rebuilt from Kafka; rules are run against historical data |
| End-to-end observability | One trace from sensor to operator action |

## 7. Product Principles

1. **Operator is the most important.** The interface is designed for 24/7 operation: dark theme, high density, hotkeys, audio notifications.
2. **Incident, not alarm.** The platform groups and correlates events so operators can work with situations.
3. **Procedure is executed, not read.** The scenario guides the operator and ensures no mandatory step is missed.
4. **Nothing is lost or duplicated.** This property is verified by tests, not declared.
5. **One delivery.** Single-machine and VM-cluster deployments use the same application artifacts and differ in configuration.
6. **Open standards.** gRPC, Protobuf, OpenAPI, AsyncAPI, OIDC, ONVIF, MQTT.

## 8. MVP Framework

End-to-end workflow: event from any source -> normalization -> correlation -> incident -> response scenario -> device commands -> closure -> audit and reporting. Operation is carried out through the operator’s web console and web administration.

Full list of functions and explicit list "outside MVP" are provided in [mvp-scope.md](mvp-scope.md).

Addition accepted ([ADR-042](../adr/0042-operational-capabilities.md)): reliable device status and realtime, plan storage, one-time maintenance, minimal responsibility transfer and current duty, certificate procedures and isolated replay. Full shift schedules and own media attachments remain after MVP.

## 9. MVP Success Metrics

| Category | Metric | Goal |
|---|---|---|
| Acceptance | Completed S1-S13 scenarios | 13 out of 13 |
| Performance | Stable load | 50,000 events/second over 24 hours, peak ×1.5 over 10 minutes |
| Delay | Event -> Incident in console | p99 ≤ 1s |
| Reliability | Losses and duplicates of incidents | 0 |
| Reliability | RTO in case of one cluster node failure | ≤ 60 seconds |
| Integration | Time for third-party developer to connect new connector according to documentation | ≤ 5 working days |

A partner pilot is NOT a condition for accepting or releasing the MVP. Currently, there are no partners; evaluation on a real-world object is discussed when an interested participant emerges.

The full list of non-functional requirements with stable IDs can be found in the [quality registry](../quality/requirements.yaml).

## 10. Constraints

- Platform: Debian 13 x86_64 - the only officially supported operating system.
- There is only one edition: Enterprise.
- Certification is not required for MVP.
- PSIM does NOT replace certified fire alarm and firefighting systems: fire automation management remains the responsibility of the fire alarm control equipment. PSIM displays, correlates, and organizes responses ([threat model](../security/threat-model.md#5-assumptions-and-responsibilities)).
- Only one tenant is used in MVP, but the tenant’s identifier is present in the data models and contracts.
