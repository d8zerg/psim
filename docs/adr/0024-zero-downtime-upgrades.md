# ADR-024. Zero-downtime upgrades and migrations

<!-- language-links -->
[English](0024-zero-downtime-upgrades.md) | [Русский](../ru/adr/0024-zero-downtime-upgrades.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-004, ADR-023, [deployment.md](../architecture/deployment.md#4-update)

## Context

A cluster upgrade N -> N+1 must run under load without interrupting processing or losing data (S12), and support rollback. During an upgrade, N and N+1 instances concurrently read and write the same topics and tables.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Stop, migrate, restart | Simple | Downtime violates S12 |
| Blue-green: a second complete platform instance | Immediate rollback | Doubles infrastructure; Kafka and PostgreSQL cannot simply be duplicated |
| **Rolling replacement with N/N+1 compatibility and expand/contract migrations** | No downtime or doubled infrastructure | Requires each schema change to span two releases |

## Decision

1. **Compatibility:** N and N+1 are compatible in message schemas (ADR-004), REST (additions only within `/v1`) and connector protocol. A signed release contains a contract snapshot; incompatible transitions are rejected before replacement.
2. **Expand/contract migrations:** N+1 includes only expansion: new tables, optional columns and indexes created `CONCURRENTLY`. Contraction, such as dropped columns and constraints, belongs in N+2. Migrations are idempotent and versioned; tooling resides in `libs/platform/pg`.
3. **Order:** expand migrations -> schema registration -> replace consumers before producers -> verification.
4. **Instance replacement:** remove an HTTP instance from new proxy assignments while a healthy replica maintains availability. For Normalizer and Correlation Engine containers, start the new version with a separate producer identity before stopping the old one: metadata loading -> Kafka assignments -> readiness -> stop the old instance. Rollback uses the same sequence. A local proxy preserves administrative addresses.
5. **Rollback:** replace instances back before contraction migrations; verify old-reader compatibility before starting it.
6. **Protection:** the installer refuses upgrades that fail compatibility preflight.

[ADR-079](0079-release-version-identification.md) defines distribution identity and dynamic versioning. Unknown expand phases require an explicit `minimum_reader`; an incompatible or unknown contract phase prevents an old reader from starting. Before contract, every node persistently records that rollback is forbidden.

Containers and systemd use saved configuration versions and a replacement journal. The trusted key is supplied independently of the release; signatures, inventory, SBOM and OCI identities are verified before installation. Modified conffiles are not replaced by templates. Procedures and commands are in [installation and upgrades](../engineering/installation-and-upgrades.md).

## Consequences

- CI migration linting checks that release migrations only expand schemas.
- The release suite includes an upgrade-under-load test.

## Verification

S12/C-12: upgrade and rollback under `LOAD-50K` with end-to-end reconciliation. Load qualification is incomplete and optional; it is excluded from step 10.1 and does not block release. The pass criteria remain unchanged for an optional run.
