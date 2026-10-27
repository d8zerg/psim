# Installation and upgrades

<!-- language-links -->
[English](installation-and-upgrades.md) | [Русский](../ru/engineering/installation-and-upgrades.md)
<!-- /language-links -->

This mechanism extends [topology delivery](deployment-topologies.md). Decisions are recorded in [ADR-024](../adr/0024-zero-downtime-upgrades.md) and [ADR-079](../adr/0079-release-version-identification.md). Infrastructure versions remain pinned; upgrading infrastructure is a separate operation.

## Version and artifacts

`python3 -I tools/version.py` prints the product version, revision, and Debian and Docker representations. An official build requires a clean annotated SemVer tag; `--release` checks this condition. Development versions are used between tags. The web build publishes the same version in `build-info.json`; private workspace packages are not released independently.

The base version comes from the nearest reachable Git SemVer tag. There is no version file or implicit initial version. The initial tag is `v0.9.2-alpha.1`, applied to committed sources. A normal build fails until a tag exists. An explicit `PSIM_BUILD_VERSION` can produce a reproducible laboratory artifact, but cannot authorize an official release.

`task release:dist` produces DEB packages, OCI archives, a CycloneDX SBOM, an installer archive, documentation, a REST/protobuf snapshot and `release.json`. `SHA256SUMS` is signed with an Ed25519 key. `ALLOW_DIRTY=true` permits qualification of a modified tree; this is not an official release. Verify using `task release:verify` with a separately obtained `PSIM_TRUSTED_KEY`.

Never replace the contents of a published version. Store temporary qualification builds separately. Installed identity is computed from the signed inventory, not only from `release.json` or a container tag.

## Initial installation

Prepare the public profile and external credentials according to the [PKI procedure](deployment-pki.md). Verify the release signature before changing the installation. `node_install.py` accepts a public release and a separately trusted key together with the node's private payload:

```sh
python3 -I tools/deploy/node_install.py \
  --archive /secure/node-1.tar.gz --sha256 PAYLOAD_SHA256 --node node-1 \
  --release /srv/releases/N --trusted-key /secure/psim-release.pub.pem
```

The private payload is not part of the public release. Transfer it through a trusted channel and verify it against an externally supplied SHA-256. Do not obtain PKI material, passwords or keys from demonstration files. Reinstalling the same payload preserves data; a different payload cannot replace the existing directory.

`--lab` explicitly permits local installation of a laboratory payload without a release signature. It is never enabled automatically from archive contents. Laboratory upgrades still verify the signatures of both versions.

## Preparing an upgrade

Retain signed versions N and N+1 and the trusted key. Verify a backup according to the established procedure. A cluster requires two healthy application replicas of every service. A single-machine deployment may stop while its sole instance is replaced; zero-downtime operation requires a cluster.

```sh
task deploy:upgrade:preflight BUNDLE=build/deployment/lab-cluster \
  BASELINE=/srv/releases/N RELEASE=/srv/releases/N+1 TRUSTED_KEY=/secure/psim-release.pub.pem
task deploy:upgrade:prepare BUNDLE=build/deployment/lab-cluster \
  BASELINE=/srv/releases/N RELEASE=/srv/releases/N+1 TRUSTED_KEY=/secure/psim-release.pub.pem
```

Preflight checks verify the signature, complete file inventory, SHA-256, SBOM, platform, SemVer ordering, component set, contract snapshots, configuration format and unchanged infrastructure. Each node repeats verification and stores both versions in its private installation repository. Verified transferred files are reused; incomplete transfers are checked again.

Changes to protobuf fields, removal of messages, RPC methods, REST operations or properties, addition of required fields, and incompatible protocol changes prevent automatic upgrades. Conservative checking of existing REST operations may require a separate decision even for a wire-compatible change.

## Replacing instances and rolling back

`task deploy:upgrade:apply` uses the same variables. Expand migrations run first, followed by consumer replacements and then producer replacements. Before replacing an instance, the coordinator checks for a healthy replica and removes the HTTP instance from new HAProxy assignments. The new version is admitted only after readiness and `/info` checks. One instance is replaced at a time.

Normalizer and Correlation Engine containers use a temporary additional replica: the new version loads metadata, receives assignments for all its Kafka groups and passes readiness checks before the old version receives `SIGTERM`. Different instance identifiers keep Kafka producer identities separate. Administrative addresses remain available through local HAProxy; the installation journal records the exact container and configuration. The same rule applies during rollback.

A failed replacement restores that instance's previous image and configuration. Other completed replacements remain journaled and can be continued or rolled back. Failed-start diagnostics are retained in the node's `lifecycle` directory. Unacknowledged commands are not retried. The public proxy may perform bounded GET/HEAD read retries after a temporary 503 or a closed connection.

`task deploy:upgrade:rollback` sequentially restores the saved N version, including its configuration. It does not reverse expand SQL migrations. This rollback is prohibited after the contract barrier.

Modified settings are not overwritten by a new template. Configuration migrations may add missing values, perform controlled renames and use inventory values. A rename conflict fails instance configuration validation before shutdown. The old file is retained for rollback.

For DEB packages, maintainer scripts check the platform and update systemd while preserving data and credentials. Conffiles in `/etc/psim-platform/defaults` retain modifications through `--force-confold`. Packages do not start all services simultaneously: the coordinator controls restart order. System processes use the same persisted configuration versions as containers.

## Schema migrations

PostgreSQL and ClickHouse store versions, phases, SQL SHA-256 and the minimum reader-plan version `minimum_reader`. An unknown expansion is accepted only with an explicit compatible reader floor. Modification of an already executed migration, incompatible expansion and an unknown contract phase are prohibited.

An automatic release contains only a restricted set of expand DDL. Non-resumable data changes and destructive commands are prohibited in expand. PostgreSQL plans are serialized by a session advisory lock and record progress after each idempotent statement; individual statements support `CREATE INDEX CONCURRENTLY`. ClickHouse uses native `psim-ch-migrate` and a PostgreSQL journal; DDL must tolerate retries after an unknown outcome.

The contract phase for an N+1 expansion runs only in the next epoch, after all old readers have been replaced. `task deploy:upgrade:contract` first persists the rollback prohibition on every node, then runs the destructive phase. An error does not automatically remove that prohibition.

## Infrastructure versions

Supported versions and transition boundaries are listed in `deploy/infrastructure/compatibility.json`; exact images are pinned in `deploy/topologies/images.json`. Unlisted target versions require separate qualification. There are no automatic major-version upgrades.

| Component | Order and gates |
|---|---|
| Kafka | One broker/controller at a time; restore ISR and quorum before the next. Finalize `metadata.version` after the rollback window |
| PostgreSQL 18 | Replicas, synchronous acknowledgment check, controlled switchover, former primary. Major upgrades require a separate procedure |
| ClickHouse | One replica, replication completion, second replica. Upgrade Keeper separately from the servers |
| Valkey | Replicas, controlled Sentinel switchover, former primary; then Sentinel instances individually while maintaining quorum |
| Keycloak | Only a qualified compatible rolling transition; incompatible database migration requires maintenance and verified recovery |
| SeaweedFS | Masters individually with quorum -> volumes individually with replication -> filer -> S3 gateway. Verify reads of existing floor plans and new uploads |

References: [Kafka](https://kafka.apache.org/43/getting-started/upgrade/), [PostgreSQL](https://www.postgresql.org/docs/18/upgrading.html), [ClickHouse](https://clickhouse.com/docs/guides/oss/update), [Valkey Sentinel](https://valkey.io/topics/sentinel/), [Keycloak](https://www.keycloak.org/docs/latest/upgrading/index.html), [SeaweedFS](https://github.com/seaweedfs/seaweedfs/wiki/Production-Setup).

## Qualification

S12/C-12 qualification is incomplete. Successful upgrade and rollback under full LOAD-50K remain mandatory before release. The temporary-additional-replica mechanism has targeted startup-order checks; its full load qualification is incomplete.

The completed cluster run replaced all 26 instances and performed a full rollback. All 1433 floor-plan reads succeeded. All 171708 SDK-acknowledged raw identifiers were found; this does not establish successful end-to-end reconciliation. A signal pause of approximately 19 seconds and incident lag violated S12 criteria. A separate LOAD-50K attempt stopped on a local SDK buffer error before services were upgraded. These results remain limitations, not a successful S12 result.

`task deploy:lifecycle:test` checks signatures, contract barriers, preservation of settings, rejection of unsigned installation and actual dpkg behavior. PostgreSQL and ClickHouse component tests check schema-version coexistence and rejection of incompatible rollback.

`task deploy:upgrade:qualify` accepts `BASELINE`, `RELEASE`, `TRUSTED_KEY`, `RUNTIME` and `RATE`. It upgrades and rolls back while the SDK continuously publishes, reconciles every acknowledged identifier in raw and normalized Kafka, checks a domain end-to-end scenario, and reads a floor plan through the public API. The full profile contains 10,000 devices across 10 sites; one site generates 30% of background events. A control alarm is added once per second. The SDK uses batch publication; actual acknowledgment rate is checked before upgrading. The generator maintains a separate identifier journal.

The generator requests 51,000 events/s to provide a small margin over the required 50,000 background events/s. The report includes measured rates, distribution by site, maximum signal interval, overall p99 and p99 for every ten-second interval. S12 limits remain unchanged: a pause of at most 5 seconds, event -> incident p99 of at most 1 second, and zero logical losses or duplicates. Raw transport retries and repeats of one outbox publication are counted separately; the normalized stream and logical effects are checked according to ADR-006. A functional run at a lower load does not qualify LOAD-50K.

`PSIM_LAB_ID=s12` selects a separate VM laboratory, configuration directory and factory record. The VM container is named `psim-platform-vm-s12`; directories are `.cache/deployment/s12` and `build/deployment/s12`. The ordinary laboratory retains its existing paths and identities. This preserves failed-run results and allows S12 to run against a clean dataset. The container profile checks Kafka partition handover between versions.

`python3 -I tools/deploy/seaweed_upgrade.py qualify` performs the exact 4.47 -> 4.48 transition in an isolated Docker environment with three masters, two volumes, two filers, PostgreSQL and two S3 gateways. It checks continuous reads, new-object uploads and rejection of anonymous access. This verifies the version transition, not independent physical VM failure domains.
