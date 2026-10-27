# Administration and recovery

<!-- language-links -->
[English](administration-and-recovery.md) | [Русский](../ru/engineering/administration-and-recovery.md)
<!-- /language-links -->

Status: Accepted. Decision: [ADR-081](../adr/0081-administration-and-recovery.md).

## Configuring access

`psimctl` is a native C++ program using the platform's Kafka, PostgreSQL, S3, ClickHouse and HTTPS clients. Physical backups use the native `pg_basebackup` and `pg_verifybackup` utilities; encryption uses OpenSSL CMS. The CLI image includes these utilities. The DEB requires OpenSSL and tar and recommends `postgresql-client-18` for physical backups and configuration export. Install PostgreSQL 18 client tools before these operations, or use the pinned administration image. Existing commands `psim-gateway-admin`, `psim-pg-migrate`, `psim-ch-migrate`, `psim-audit-tool`, `psim-console-tool` and `psim-report-tool` remain available for one release.

The sample configuration is `deploy/operations/psimctl.example.json`. Replace endpoints, deployment name, data-owner UIDs, certificate and secret paths, the allowed DLQ-source table and WAL namespace. Obtain UIDs from the generated inventory, not the example. The PostgreSQL certificate CN must match the role, such as `postgres`. Gateway administrative TLS uses `ca.pem`, `admin.pem` and `admin.key` in a separate directory.

Secrets and private keys must be regular `0400` files owned by the client process UID. Do not pass passwords or JWTs as command arguments. The administrative OS account owns the configuration and journal directory. `actor`, UID, operation identifier, parameters and result are written to `operations.jsonl` and synchronized to disk. Remote APIs also perform their own authorization and auditing. The local journal does not replace the protected product audit chain.

## Independent storage

Backups require a separate S3 service outside the operational SeaweedFS cluster. Its bucket must not be named `psim-plans`. Separate permissions:

- Backup writer: object creation in the designated bucket only, without reading, deletion or retention changes.
- Backup reader: a separate identity with read and list permissions.
- Recipient private key: available only in the recovery environment; the schedule needs only the public RSA certificate, with at least 3072-bit keys.
- Retention management and deletion: a separate storage-administration role.

Encryption uses CMS AES-256-GCM and RSA-OAEP/SHA-256. Each large file is split into parts of at most 8 MiB. The manifest records sizes and SHA-256 values, is encrypted, and is written last. An unknown write outcome is not reported as success. Incomplete objects without a manifest are not confirmed backups. Remove them through a separate storage policy, without using the operational account.

The initial configuration uses a seven-day PITR window, hourly floor-plan backups and eight-day protection for unreferenced objects. The CLI checks that protection exceeds the sum of the window and backup interval. Resource Catalog deletes unreferenced objects no sooner than eight days after upload expiry. Upload-expiration checks remain in place. Enable schedules only after every Resource Catalog instance uses this cleanup policy. Returning to the earlier cleanup policy before the active PITR window ends breaks object protection.

External target storage is not yet configured. Schedules are not enabled automatically. `wal_archive: false` keeps archiving disabled until independent S3 is configured. Changing to `wal_archive: true` sets PostgreSQL `archive_mode=on`, `archive_timeout=60s` and a `psimctl backup wal-push` invocation. For an existing Patroni installation, apply parameters through its DCS and perform the required rolling restart. Place archiver configuration and its separate secrets on every PostgreSQL node, including potential primaries. Do not give the archiver the reader's private key.

The archiver retains local receipts with WAL SHA-256 values. `wal_namespace` must be unique to the physical installation and must not be reused for a new cluster. If PUT has an unknown outcome and no local receipt exists, a separate reader identity must verify the object; this outcome is not automatically successful. Never delete WAL to resolve an archiver error.

## Creating backups

```text
psimctl --config /etc/psim-platform/psimctl.json backup create
psimctl --config /etc/psim-platform/psimctl.json backup create --component plans
psimctl --config /etc/psim-platform/psimctl.json backup inspect SNAPSHOT_ID
psimctl --config /etc/psim-platform/psimctl.json backup status
```

A full operation copies floor plans, physical PostgreSQL data, ClickHouse, Kafka descriptions and compacted topics, product configuration and the protected export. After the physical backup, it creates a named restore point, confirms archival of that point's WAL, and copies floor plans again. The manifest records the object-coverage boundary.

ClickHouse executes `BACKUP DATABASE default` to the `backups` disk. Its directory is a separate temporary volume at `/backups`; the operator client must see the same directory. The archive is deleted after transfer to external S3. The temporary volume is not an independent backup. It must have space for the full archive and permissions for the ClickHouse owner.

Configuration export preserves schemas and allowed configuration tables. Active incidents, runs, side-effect queues, replay jobs, connector sessions and connector registrations are not imported as configuration. Revoked-certificate information is retained. Import is allowed only when product schemas are absent and infrastructure roles already exist. Restore floor-plan objects before their references.

Prepare the complete protected export in `protected_directory`: deployment configuration, trust, service keys and credentials, independent audit checkpoints, Keycloak configuration and the current revocation policy. The physical PostgreSQL backup includes the Keycloak, Registry and filer-metadata databases. External CA keys are stored separately according to the external PKI procedure. Never give the archiver access to the backup recipient's private key.

`backup.status_file` must point to a file in the node's monitoring volume directory, visible to the agent as `/var/lib/monitoring/backups.json`. The parent directory must be owned by and accessible to the monitoring UID. The summary itself contains no secrets and receives `0644` permissions.

Ready-to-use systemd units are in `deploy/operations/`. Run a full backup daily and a separate floor-plan backup hourly. The `backup status` heartbeat runs once per minute, updates collection status and preserves earlier success timestamps. Never replace the time of the last successful backup with a heartbeat or failed-attempt time.

## Recovery

Retain a full base backup predating the start of the PITR window and an uninterrupted WAL chain from it through the window's end. Enable deletion of older backups only after verifying that chain.

Close external ingress and stop application consumers and external side effects. For cluster recovery, use fresh control-cluster identities and a separate clean environment. Do not start a new Patroni cluster against the old DCS or attach old disks to new Kafka/Keeper clusters.

```text
psimctl --config /etc/psim-platform/recovery.json backup restore-plans SNAPSHOT_ID
psimctl --config /etc/psim-platform/recovery.json backup restore-postgres SNAPSHOT_ID --directory /recovery/postgres
psimctl --config /etc/psim-platform/recovery.json backup recovery-check SNAPSHOT_ID
psimctl --config /etc/psim-platform/recovery.json plans verify
```

Start PostgreSQL with the restored data directory and the same major version. Recovery configuration contains the path to `psimctl backup wal-fetch` and the named point from the manifest. The reader configuration and private key must be accessible to the PostgreSQL UID at the same path inside the process. Recovery pauses at the target point; `recovery-check` checks the target LSN, pause state, point name and actual files. Early readiness of read-only PostgreSQL does not mean recovery is complete.

For PITR between full backups, choose a base backup completed before the target time and a floor-plan backup whose coverage boundary is at or after that time. Specify `TARGET_TIME_UTC` in UTC, with seconds or six fractional-second digits and a `Z` suffix. Run restoration as the PostgreSQL data-owner UID or root; reader credentials used inside `restore_command` must belong to the PostgreSQL UID.

```text
psimctl --config /etc/psim-platform/recovery.json backup restore-plans PLANS_SNAPSHOT_ID
psimctl --config /etc/psim-platform/recovery.json backup restore-postgres BASE_SNAPSHOT_ID --directory /recovery/postgres --target-time TARGET_TIME_UTC --plans-snapshot PLANS_SNAPSHOT_ID
psimctl --config /etc/psim-platform/recovery.json backup recovery-check BASE_SNAPSHOT_ID --target-time TARGET_TIME_UTC
```

The CLI checks base-backup and floor-plan coverage boundaries and configures `recovery_target_time` and pause. `recovery-check` verifies the actual target setting, paused state and every referenced file. With hourly floor-plan backups, consistent metadata-and-file RPO is limited to one hour. A smaller PostgreSQL RPO alone does not shorten that interval.

Reconciliation reads every referenced floor-plan version and verifies its size and SHA-256. Missing or corrupted objects produce a nonzero exit code. Unreferenced objects are reported separately and are not deleted. Preserve reconciliation results in the recovery-exercise report.

After verification, resume WAL replay using native `pg_wal_replay_resume()` and wait until recovery ends. In a cluster, recover the primary first, then create replicas using native physical synchronization. Check synchronous replication and automatic failover.

```text
psimctl --config /etc/psim-platform/recovery.json backup restore-kafka SNAPSHOT_ID --offline
psimctl --config /etc/psim-platform/recovery.json backup restore-clickhouse SNAPSHOT_ID
psimctl --config /etc/psim-platform/recovery.json backup restore-protected SNAPSHOT_ID --directory /recovery/protected
psimctl --config /etc/psim-platform/recovery.json backup restore-config SNAPSHOT_ID --empty-installation
```

Kafka recovery requires product topics to be absent. It transfers configurations, partition counts, replication, keys, values, headers and tombstones of compacted topics. Original physical offsets and consumer offsets are not transferred. Do not use the restored changelog with an old working group and assume its positions are unchanged: reconcile input history and groups before starting processing in the clean environment. History outside retention comes from ClickHouse and backups. Console rebuilding rejects incomplete Kafka history.

Extract the trust backup into a separate directory for inspection without automatically replacing active keys. Apply the current external revocation policy and reissue compromised keys. After promoting PostgreSQL, run `backup recovery-secure SNAPSHOT_ID --offline`: this disables every restored connector registration, clears sessions and sends Keycloak `logout-all`. Use `certificates recover` with a new certificate for each connector; revoke previous fingerprints through normal gateway administration. Do not reopen external ingress before checking new logins, MFA, mTLS and rejection of expired and revoked keys.

## Operational administration

```text
psimctl diagnose
psimctl support --output /secure/support.tar.gz
psimctl dlq inspect --topic psim.dlq.normalizer.v1 --partition 0 --from-offset 0 --to-offset 100
psimctl dlq retry --topic psim.dlq.normalizer.v1 --partition 0 --from-offset 0 --to-offset 100 --apply
psimctl normalizer cleanup --tenant TENANT_ID --source SOURCE_ID --apply
psimctl history reload --group psim-event-history --apply
psimctl projection console rebuild
psimctl projection facts rebuild TENANT_ID FROM_MS TO_MS
psimctl migrate postgres expand MANIFEST_JSON
psimctl migrate clickhouse expand MANIFEST_JSON
psimctl audit verify EXPORT_JSONL TRUSTED_KEYS_JSON CHECKPOINT_DIRECTORY
```

The existing C++ tools retain their `PSIM_PG_*`, `PSIM_CH_*` and `PSIM_KAFKA_*` settings through compatibility commands. Invoke compatibility commands without `--config`. Backup, diagnostics, replay, DLQ and cursor-cleanup commands use CLI settings from `--config`. The support package contains only generated diagnostic aggregates; it excludes configuration, tokens, private keys, HTTP bodies and raw logs.

Inspect DLQ without writing first. Retry accepts only sources allowed by `dlq_sources` for the configured processing stage. It requires the original partition and preserved bytes. The range is limited to 10,000 offsets. Publication and the retry's own position are committed in one Kafka transaction; retrying a completed range does not publish messages again. The original working consumer group is not rewound.

Normalizer cleanup requires every instance to be stopped and the source to belong to an archived device. The CLI writes each epoch's tombstone to its original partition. During recovery, Normalizer deletes only the matching cursor and preserves other epochs and sources. After cleanup, do not return the retired source to service using its old identity and sequence.

History reload requires all instances of the selected loader to be stopped and no prepared blocks to remain in PostgreSQL. The CLI resets only the allowed group's offsets to the available retention start. It neither restores unavailable history nor clears existing tables. For lost tables, first restore schemas and an independent backup, then load the available continuation. Incident-fact recomputation has a separate command.

## Diagnostic replay and certificates

```text
psimctl replay rules --rule RULE_ID --request REQUEST_JSON --replay-id REPLAY_ID
psimctl replay result --id JOB_ID
psimctl replay response --input INPUT_JSON --scope SCOPE --replay-id REPLAY_ID --output /secure/replay.json
psimctl certificates inspect --request REQUEST_JSON
psimctl certificates renew --request REQUEST_JSON
psimctl certificates revoke --request REQUEST_JSON
psimctl certificates recover --request REQUEST_JSON
```

Rule replay uses the existing HTTPS API with JWT, rule versions, historical range, access scope and separate results. A request retry preserves `Idempotency-Key`. Response replay reads a prepared file containing a fixed plan version and history, receives a clean environment and writes a separate result. It does not load operational storage clients or publish commands or notifications. `task response:test` retains a verification scenario that runs in a separate network namespace.

`certificates` checks that the action matches request contents and invokes the gateway's existing mTLS/JWT administration interface. It uses the original `request_id`, expected version and reason. After an ambiguous outcome, repeat the same request instead of creating a new identifier. External PKI handles trust renewal and service-certificate issuance according to the [procedure](deployment-pki.md).

## Recovery exercises and objectives

| Topology | PostgreSQL RPO target | Floor-plan RPO target | Full-recovery RTO target |
|---|---|---|---|
| Single machine | 5 minutes | 1 hour | 4 hours |
| Cluster | 5 minutes | 1 hour | 2 hours |

These are initial operational objectives, not measured guarantees. Other components receive daily full backups; lower RPO requires more frequent schedules. The available-backup window must cover at least seven days. Independent storage is responsible for retention and deletion checks; the CLI does not automatically delete backups.

`task deploy:operations:test` creates independent S3 processes, PostgreSQL, Kafka and ClickHouse, invokes the built C++ CLI, and restores data into clean directories and processes. It checks write-without-read permissions, encrypted backups, DLQ, tombstones, compacted topics, floor plans and the PostgreSQL restore point. The report is `build/operations/report.json`; test secrets are removed afterward.

For operational acceptance of each topology, record data volume, last acknowledged write time, failure start, selected restore point, readiness time and complete reconciliation results. Measure RPO and RTO after data loss. Verify normal login, MFA, connector mTLS, rejection of old keys and PITR before object cleanup. External target storage is not yet available; its performance and full recovery of both topologies remain unqualified. The container test does not satisfy this acceptance criterion.
