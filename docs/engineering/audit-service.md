# Audit Service

<!-- language-links -->
[English](audit-service.md) | [Русский](../ru/engineering/audit-service.md)
<!-- /language-links -->

The Audit Service loads actions from `psim.audit.v1`, builds a separate SHA-256 chain on the tenant, and stores Ed25519-signed checkpoints outside PostgreSQL. The solution and trust boundaries are defined in [ADR-021](../adr/0021-audit-hash-chain.md).

## Storage and Execution

`services/audit-service` builds `psim-audit-service` and `psim-audit-tool`. The runtime process does not perform migrations or receive the schema owner’s password. The installer creates an application role without SUPERUSER, CREATEDB, CREATEROLE, and ownership membership, grants CONNECT to the target database, and executes it as the owner:

```sh
psim-audit-tool migrate psim_audit
```

The utility connection is set via `PSIM_PG_HOST`, `PSIM_PG_PORT`, `PSIM_PG_DATABASE`, `PSIM_PG_USER`, `PSIM_PG_PASSWORD`, `PSIM_PG_SSLMODE`, `PSIM_PG_SSLROOTCERT`, `PSIM_PG_SSLCERT`, and `PSIM_PG_SSLKEY`. By default, TLS checks the hostname and CA. The password is taken from the installer’s secure environment; it is not an argument to the command.

Migrations create `audit.entries`, `audit.checkpoints`, inbox/outbox, and identification log cursor. Role `psim_audit` only has SELECT and INSERT on entries and checkpoints. Triggers block UPDATE, DELETE, and TRUNCATE even if the rights are incorrectly granted. The application checks for no privileges before execution. A regular role cannot disable triggers. Superuser can do this, so an external checkpoint is required.

**Main Settings:**

| Field `settings` | Purpose |
|---|---|
| `pg_user`, `pg_password_file` | Runtime role and its secret file |
| `signing_key_file` | Private Ed25519 key in PEM format, secret field |
| `trusted_keys_file` | JSON object `key_id -> public key PEM` |
| `checkpoint_directory` | Separate existing directory for external checkpoints |
| `checkpoint_interval_ms` | Interval up to 300,000 ms, default 300,000 |
| `checkpoint_records` | Limit up to 10,000 new records, default 10,000 |
| `tls_certificate_file`, `tls_private_key_file`, `tls_ca_file` | TLS API |
| `issuer`, `jwks_url`, `jwks_ca_file` | Trusted JWT source |

Kafka, Schema Registry, and PostgreSQL use common stream runtime settings. `allow_plaintext` is only allowed in an isolated local environment. Consumer group - `psim-audit-service`, starting position - earliest in the available retention. Kafka key equals the tenant’s UUID. Repeating sources with different actions or times are rejected in DLQ. The body of an input record is limited to 1 MiB, as per existing producer outboxes. Reading is limited to 8 MiB per database page; the API page can contain fewer records than requested and continue with the cursor. For this budget, the standard `pg_max_result_bytes` of 16 MiB or more is maintained.

The public key identifier is SHA-256 of the 32-byte public key, which is the last 32 bytes of its Ed25519 DER SubjectPublicKeyInfo encoding. Trusted keys are set independently of the database and export. During rotation, a new public key is added, the private key is switched, and previous public keys are saved for history verification.

## External Checkpoints

Files have the format `<checkpoint_directory>/<tenant_uuid>/<audit_seq>.json`. The process does not overwrite existing files: only an identical checkpoint can be repeated. First, fsync the file and directories, then store its copy and the Kafka outbox record in a PostgreSQL transaction. If a COMMIT fails, the remaining external file restores the copy and publication. Two instances use one independently stored archive and shared keyring.

The archive is stored on a separate storage or volume, inaccessible to PostgreSQL administrators. The private key itself is also inaccessible to them. Backup should save the archive, keyring, and database, and verification must check their consistency. Deleting all records or checkpoints from the database does not remove evidence from the archive.

Periodic checks recalculate the chain, verify external signatures and chain heads, then record new entries. An error does not create a new checkpoint for a broken chain. The absence of the archive and integrity violations worsen readiness; operators must respond to unhealthy `audit` components and messages `audit background operation deferred`. Normal event processing does not delete damaged data or attempt to automatically rewrite the chain.

Protection against consistent overwriting applies until the last external checkpoint. Checking for uncheckpointed suffixes does not prove absence of full replacement. With an available archive, the window is limited by the configured interval; export creates a checkpoint for its chain head immediately.

## Searching, Verifying, and Exporting

The API uses HTTPS and JWT with role `auditor`. Product administrator actions do not grant it automatic auditor rights.

- `GET /v1/audit/records` - Search by time of the original action, user, action prefix, object type and identifier, site, and outcome. Filter parameters have prefix `query.`, e.g., `query.action=incident.`. Order - descending `audit_seq`; up to 1,000 records. The cursor is linked to the tenant and query. Site auditor explicitly provides it; global actions are available to the auditor with scope `*`.
- `POST /v1/audit:verify` - Body with `fromSeq` and `toSeq`; zero in fromSeq means the beginning, and zero in toSeq means the end of the chain. The response contains `intact`, `recordsChecked`, `firstBrokenSeq`, `detail`, and the latest checkpoint. Even for a limited range, proof is checked from the beginning. The operation requires scope `*`, as integrity relates to the entire chain.
- `GET /v1/audit/records:export` - JSONL with all snapshot records and criterion `selected` for filtering results. The full chain is needed for completeness and adjacent hashes. Therefore, export requires scope `*`. Limit - 64 MiB; exceeding returns an error without truncation. A broken chain returns `AUDIT_INTEGRITY_VIOLATION`.

Each allowed operation and failure due to verified identity are recorded through the outbox. Unverified JWT is not used as a source for tenants or users for recording; such attempts remain in the security log of the authentication boundary.

For independent verification, export, trusted keyring, and independently obtained archive copy at the time of snapshot are provided:

```sh
psim-audit-tool verify psim-audit.jsonl trusted-keys.json checkpoints
```

The utility does not use a network or PostgreSQL. It calculates hashes, verifies signatures, and checks the top of the snapshot. The last external checkpoint must match the top of the export. A newer archive requires a new export; for storing historical evidence, an independent snapshot of the archive is kept along with it. Keys from the export itself are not accepted. A completion code of 0 indicates a successful check, 1 indicates a violation or invalid evidence, 2 indicates an incorrect call.

## Identity Log

The Audit Service can import the Keycloak log through the read-only Admin REST API. Settings `identity_base`, `identity_client`, `identity_secret_file` define a dedicated client with `view-events`, `view-realm`, `view-users`. In the realm `psim`, user and administrative events must be enabled; resource representations are not required. The client’s secrets are encoded as form fields and never enter the audit.

The [Keycloak Admin REST API](https://www.keycloak.org/docs-api/26.8.0/rest-api/index.html) is used. LOGIN, LOGIN_ERROR, LOGOUT, REFRESH_TOKEN_ERROR, and administrative events are loaded. Only the operation type, event identifiers, user and client, error code, resource type and path, and client address are included in the audit. Passwords, tokens, credentials, and complete resource representations are not copied.

A tenant of a known user is determined from the single attribute `psim_tenant` during import. This attribute must remain unchanged throughout the lifetime of the identifier; transferring accounts between tenants requires a new identity. Events of unknown or deleted users and administrators from another realm are directed to the explicitly configured `identity_security_tenant`. This is a centralized audit tenant with access provided separately.

The cursor and outbox are tracked atomically. A fixed upper limit for the window, a delay of two seconds, and replay of the previous minute are used; repetitions are deduplicated by event identifier. Batches of 100 events limit the performance of one process. Retention in Keycloak must exceed the maximum permitted importer lag; deleted events before import cannot be restored. Disabling the log or failing to read worsens readiness.

## Producers and Scenarios

This table identifies audit producers; complete scenario qualification is tracked separately.

| Scenario | Actions and Producers |
|---|---|
| S1 | Connector Gateway: `connector.REGISTRATION_ACTION_*` for registration, renewal, revocation, and shutdown; `access.denied` for rejected registrations and sessions; Resource Catalog: mapping configuration; Keycloak: administrative operations |
| S2 | Resource Catalog: `catalog.*` for objects, plans, imports, and configurations; `access.denied` |
| S3 | Resource Catalog: changes to normalization. Normalization of regular events is stored in Event History and does not create separate audit records for each measurement |
| S4 | Correlation Engine: `rule.draft_saved`, `rule.validated`, publishing and archiving rules, trial runs; automatic maintenance suppression |
| S5 | Incident Service: creation and transitions of incidents, signal attachment |
| S6 | Incident Service and Event History: data access failures; Keycloak: sign-ins. Displaying and delivering console frames is checked in its own connection log |
| S7 | Incident Service: acceptance, comments, transfer, priority change, resolution, and closure; Response Engine: plan versions, step transitions, Override, and replay diagnosis |
| S8 | Command Service: requests, approvals, cancellations, states, and results; Connector Gateway: registration and session lifecycle |
| S9 | Response Engine: policies, duties, SLA violations, escalation, and absence of duty; Notification Service: states, receipts, and verification of unknown results |
| S10 | Audit Service: search, verification, export, and access failures; Keycloak: role management and account management |
| S11 | Services: final states of restored actions without re-logging. Technical restart of processes, broker status, and node failure are reflected in logs and infrastructure metrics |
| S12 | Management of release and updates relates to the installer and operational procedures. Its actions must be published in `psim.audit.v1` with a stable operation identifier during cluster delivery |
| S13 | Projection Service: `report.read`, `report.export`, and `access.denied` through outbox; Audit Service accepts a common contract |

Changes to passwords, roles, and clients are recorded by the Keycloak administrative event importer. Changes to the database are detected by chain integrity checks; they are not considered normal API actions of the application. The final cross-verification of S1-S13 remains in the system qualification phase.

## Tests

```sh
task audit:test
task test:audit-integrity
task audit:sanitizers
task audit:coverage
```

Tests use native processes in Debian 13 containers on the Compose network with real Kafka, PostgreSQL, and Keycloak. Pre-launch history, repetitions, two copies of the service, access failures, actual sign-ins, Incident Service operations, SIGKILL, and SQL interference are tested separately from the clean hash model.
