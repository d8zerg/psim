# ADR-081. Native Administrative Operations and Backup Copies

<!-- language-links -->
[English](0081-administration-and-recovery.md) | [Русский](../ru/adr/0081-administration-and-recovery.md)
<!-- /language-links -->

- Status: Accepted
- Related decisions: ADR-023, ADR-034, ADR-042, ADR-046, ADR-052, ADR-080

## Solution

`psimctl` implements administrative operations and recovery using existing Kafka, PostgreSQL, S3, HTTPS, and ClickHouse libraries. Monitoring is performed by a separate component `psim-monitoring-agent`, which has its own package, image, and lifecycle. It uses platform-specific clients and a common file input/output library, independent of the CLI, and does not receive its recovery operations. The agent’s image uses a minimal pinned base image without administrative utilities.

The execution of migration releases falls under `psim-pg-migrate release`. C++ checks checksum, journals executed statements, maintains locking, and verifies the schema epoch barrier before contract migrations. The Python installer delivers verified artifacts and runs the native tool.

The physical PostgreSQL copy is created by regular `pg_basebackup` and `pg_verifybackup`; WAL is archived with a separate call `psimctl backup wal-push`. Native utilities run without a shell, with explicit arguments and environment. The CLI retains current version commands and contract examples, as well as access to previous gateway, migration, audit, and projection tools. Previous names are retained for one release.

Copies are stored in an independent S3, separately from the working SeaweedFS and bucket `psim-plans`. The backup writer account has no read access; a separate read account and private key are available only during recovery. Copy objects are encrypted with CMS AES-256-GCM using a public recipient certificate and RSA-OAEP/SHA-256. Large artifacts are divided into limited parts. The fully encrypted manifest is published last. An unknown write outcome is not considered a success.

ClickHouse creates a regular `BACKUP` in a separate temporary disk. The CLI encrypts the archive and sends it to an external storage; the temporary disk is not considered a backup copy. External storage and permissions must be set before scheduling is enabled.

After the physical copy, a named recovery point is recorded, archival of its WAL is confirmed, and then plan objects are copied again. The protection period for unreferenced objects is eight days; it must exceed the PITR window and copying interval. Recovery stops at the target point. Order: objects -> PostgreSQL -> file verification. Objects without references are reported and automatically not deleted.

Diagnostic replay and DLQ processing have different commands. Rules are applied to the existing API with access scope and version checks; local replay scenario uses a pure C++ executor without working clients. DLQ is published only with explicit `--apply`, with atomic recording of its own position in replaying. Changing original consumer offsets is not required.

Due to the need for regular utilities, the `psimctl` image uses a pinned Debian-based PostgreSQL image. Restrictions such as non-root, read-only, and lack of capabilities are preserved. This is an exception for the administrative image compared to the minimal application service images under ADR-034.

## Verification

Native module tests, real Kafka transactions for DLQ, independent S3 processes with different permissions, physical PostgreSQL recovery, and SHA-256 plan verification. Qualification of the target external storage, complete clean environments of both topologies, user login, and trust restoration are performed by separate operational exercises. A small container run does not meet these criteria.
