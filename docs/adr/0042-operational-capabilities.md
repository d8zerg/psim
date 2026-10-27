# ADR-042. Operational capabilities within MVP scope

<!-- language-links -->
[English](0042-operational-capabilities.md) | [Русский](../ru/adr/0042-operational-capabilities.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-017, ADR-019, ADR-020, ADR-023, [operational capabilities](../architecture/operational-capabilities.md)

## Context

Event and incident processing also requires floor plan storage, reliable current state after connection loss, maintenance mode and shift handover. Certificate and replay requirements need explicit operating procedures and isolation of diagnostic runs. These capabilities are part of the platform's operational scope.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Defer all additions | Retains the previous development scope | No testable maintenance or responsibility handover behavior; file recovery remains undefined |
| Full media, shift scheduling, PKI and equipment management subsystems | Broad capabilities | Substantially expands the MVP for one engineer |
| **Minimum capabilities in existing contexts** | Covers concrete operational scenarios that can be verified before release | Additional implementation work and compatible contract extensions |

## Decision

1. Adopt the minimum scope in [operational-capabilities.md](../architecture/operational-capabilities.md) as permanent acceptance requirements.
2. Store floor plans through the `FileStore` port and an S3-compatible adapter. ADR-046 defines the SDK and storage choice, ADR-077 the topology, and ADR-051 integration with Resource Catalog. Platform-hosted incident attachments remain outside the MVP.
3. Model resource lifecycle, connectivity, observed state and freshness separately. Reconnection requires fresh state confirmation. Provide device, zone and connector snapshots and realtime deltas using ADR-019.
4. Provide one-time maintenance intervals with a mandatory end and an explicit suppression policy. Retain all events; never suppress fire events or P1.
5. Assign duty explicitly and transfer unclosed incidents with recipient confirmation, preserving SLA and scenario state. Calendar scheduling and automatic substitutions remain outside the MVP.
6. Use external PKI and documented initial setup, renewal, revocation and recovery procedures. Do not embed a proprietary CA in the platform.
7. Isolate diagnostic replay from production commands, notifications and incidents using permissions, topics and storage. Projection recovery and production DLQ processing remain separate authorized operations.
8. Maintain contribution, vulnerability reporting, compatibility and release support policies. Demonstration data is synthetic only.

## Consequences

- These capabilities require no new microservices; file handling adds an external storage component.
- Scenarios S1-S13 include operational behavior checks, reflected in the threat model and verification matrix.
- Acceptance requires evidence for each capability. Completion of an earlier subsystem does not establish that an added capability is implemented.
- Extend the API before implementing each capability. An accepted ADR alone does not establish a working API or test.

## Verification

Verify file recovery, freshness after buffered delivery, realtime resumption, automatic maintenance completion, responsibility handover, certificate rotation and revocation, and absence of production side effects during replay. The verification registry records the methods; documentation passes `task docs:check`.
