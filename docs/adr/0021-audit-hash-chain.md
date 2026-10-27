# ADR-021. Tamper-evident audit hash chain

<!-- language-links -->
[English](0021-audit-hash-chain.md) | [Русский](../ru/adr/0021-audit-hash-chain.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** [AuditChain aggregate](../domain/aggregates.md#7-audit), [guide](../engineering/audit-service.md), S10

## Context

The log must detect modified and deleted entries, including coordinated hash rewrites by a PostgreSQL administrator. Database privileges restrict ordinary processes but not superusers. Trust therefore requires checkpoints outside PostgreSQL and an independently trusted public key.

## Options considered

| Option | Properties |
|---|---|
| INSERT and SELECT privileges only | Do not protect against a PostgreSQL administrator |
| Linear SHA-256 chain with externally signed checkpoints | Simple verification; sequential writes per tenant |
| Merkle tree | Compact inclusion proofs; more complex implementation |
| External audit database | Another distribution subsystem |

## Decision

1. Audit Service consumes `psim.audit.v1` in a separate group from the beginning of available retention. The Kafka key must equal the tenant. Each tenant has a continuous `audit_seq` starting at 1. The source idempotency key is `envelope.causation_id`, falling back to `envelope.message_id`. A duplicate with changed content is rejected.
2. The first entry's `prev_hash` is 32 zero bytes. `hash = SHA-256(prev_hash || canonical(record))`. Canonical representation is deterministic Protobuf `AuditChainEntry` with `hash` and `prev_hash` cleared; it includes tenant, sequence, source identifier, action and both timestamps. Schema version and canonicalization implementation are part of the proof format. Changing the format requires a new identifier and a compatible verifier.
3. A per-tenant transaction advisory lock serializes appends and checkpoint commits across all instances. Kafka partition ownership does not replace the database lock. Kafka acknowledgement follows COMMIT. Redelivery does not create another entry.
4. A separate owner-role process runs migrations. The runtime role does not own the schema and has only SELECT and INSERT for entries and checkpoints. A trigger also forbids UPDATE, DELETE and TRUNCATE. The service refuses to start with a privileged role. Inbox/outbox receive separate privileges required for delivery.
5. When new entries exist, a checkpoint is created at least every five minutes or after 10,000 entries accumulate; export immediately fixes its own chain head. Ed25519 signs the `psim.audit.checkpoint.v1` prefix, a zero separator and deterministic Protobuf `AuditCheckpoint` with its signature cleared. Signed fields include tenant, sequence, hash, time and `key_id`. The key identifier is SHA-256 of the 32-byte Ed25519 public key.
6. First, the signed file is synchronized and published without overwrite in a separate directory; then a transaction stores its copy and the Kafka outbox. Publication synchronizes the file and directories. An incomplete COMMIT is recovered from the external copy. Checkpoints are not appended as audit actions, preventing a self-generating event stream.
7. The checkpoint directory is outside the PostgreSQL volume and administered independently of the database administrator. That administrator must receive neither the signing key nor archive write permissions. A trusted keyring supplies public keys; exports do not establish key trust. Rotation retains previous public keys.
8. Verification recomputes the chain from the beginning and checks all available checkpoints, including external files absent from the database. The requested range determines the reported entry count but does not weaken proof verification. Ordinary tampering identifies the first damaged entry; coordinated rewriting is detected at the first mismatched checkpoint.
9. JSONL export contains the request, the tenant's complete chain with `selected` flags, checkpoints and the snapshot head. This proves continuity and selection completeness without trusting search results. Export and integrity verification require the auditor role and `*` scope; search with an explicit site allows an auditor of that site. Each export is limited to 64 MiB; exceeding it returns an error without truncation.
10. Audit Service access denials and its own read, verification and export operations use a durable outbox. Audit records exclude secrets, JWTs, passwords and file contents.

## Guarantee boundaries

A database administrator can disable triggers and rewrite tables but cannot reproduce an earlier signature for a changed head. Without an external checkpoint, a coordinated rewrite of the entire chain cannot be detected. The suffix after the latest checkpoint is not yet protected against complete coordinated replacement. With the archive available, the checkpoint interval bounds this window. Archive loss or unavailability degrades readiness and blocks new checkpoints; it is an operational failure, not successful verification.

Simultaneous compromise of the database, archive and signing key is outside this guarantee. Unacknowledged events beyond Kafka retention cannot be recovered from the topic. The service cannot prove the authenticity of actions from a compromised producer; Kafka ACLs restrict publication rights.

## Verification

Canonicalization and signature unit tests are supplemented by two real processes, Kafka, PostgreSQL and Keycloak. Checks cover history preceding startup, duplicates, continuity, writer termination, runtime-role restrictions, search, isolated export verification and database-superuser tampering.

Ed25519 uses one-shot EVP as specified by the [OpenSSL documentation](https://docs.openssl.org/3.3/man7/EVP_SIGNATURE-ED25519/).
