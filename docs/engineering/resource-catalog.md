# Resource Catalog

<!-- language-links -->
[English](resource-catalog.md) | [Русский](../ru/engineering/resource-catalog.md)
<!-- /language-links -->

`psim-resource-catalog` stores locations, device types, devices, sources, plan versions, and one-time maintenance windows. [ADR-051](../adr/0051-resource-catalog.md) defines transaction boundaries and interactions with the Gateway. REST is described in [contracts](../../contracts/proto/psim/api/v1/catalog.proto).

## Launch and Testing

```sh
task catalog:test
task catalog:bench
task catalog:sanitizers
task catalog:coverage
```

By default, tasks create a separate Compose project `psim-catalog-test`. `PROJECT` allows selecting another testing environment. The test starts two instances, infrastructure, and Kafka consumers. The created project is deleted along with its test volumes; only the testing processes stop in the existing environment. Diagnostics and reports are located in `build/resource-catalog/<preset>/`.

The service is provided as a separate Debian package and image `psim-platform/psim-resource-catalog`. Example of local configuration: [resource-catalog.yaml](../../deploy/local/config/catalog/resource-catalog.yaml). The local environment allows plaintext connections to testing PostgreSQL, Kafka, and Keycloak; REST remains HTTPS. Secure connections and secrets of the environment are required for delivery. `pg_user`, `pg_database`, and `pg_port` allow using a separate role with access only to the `catalog` schema; `registry_ca_file`, `registry_certificate_file`, and `registry_private_key_file` set trust for Schema Registry. The local Catalog profile increases the SQL result limit to 128 MiB for pages up to 1000 devices.

## API and Permissions

JSON uses Protobuf formatting: lowerCamelCase, string enum names, versioning with strings. Unknown and repeated fields are rejected. Errors have type `application/problem+json` and a stable error code.

- `GET /v1/locations`, `/v1/device-types`, `/v1/devices`, `/v1/connectors`, `/v1/maintenance-windows` return pages with `pageSize`, `pageToken`, and `nextPageToken`.
- `POST` to a collection creates a resource. `Idempotency-Key` is a required UUID. Exact duplication returns the original result; other content with the same key is rejected.
- `PUT` replaces a resource’s mutable fields; `If-Match: "N"` is required. The response contains `ETag`. The path and body identifier must match.
- `POST /v1/locations/{id}:archive` requires an empty location and current version.
- `GET /v1/devices` supports filters `locationId`, `connectorId`, `deviceTypeId`, `state`. The location includes nested zones.

JWT is verified against the configured issuer, audience `psim`, and JWKS. `psim_tenant` is required. `psim_scopes` is a JSON array of site UUIDs or `*` for the entire tenant. Role `admin` allows writes only within that scope; role `operator` allows reading from your site. Common device types are available in the entire tenant. Viewing another resource’s data in your tenant returns 403 and audit; another tenant returns 404.

Hierarchical levels: site -> building -> floor; a zone can belong to a floor, building, or site. `siteId` is calculated from the parent and cannot change when moving locations. Device coordinates are only valid for zones on a floor with a plan. Moving zones and changing plan sizes check existing coordinates. The source identifier is calculated based on ADR-029 and matches the Gateway.

## Import and Export

`POST /v1/imports?format=json&dryRun=false` accepts JSON arrays of devices, while `format=csv` accepts CSV with headers. Limit is 100,000 rows and 64 MiB of input. `GET /v1/imports/{id}` returns status, counters, and errors with row numbers. The report contains the first 1000 errors; the overall counter counts all errors.

CSV uses fields `deviceId`, `deviceTypeId`, `zoneId`, `siteId`, `connectorId`, `externalId`, `name`, `sources`, `position`, `state`, `version`. `sources` and `position` are JSON within CSV cells. Quotes, line breaks, and escaped quotes are supported. Unknown and repeated columns are rejected.

Tasks and progress are persisted before the response is sent. After restarting, another instance continues from the first unrecorded row. Reimporting via `(connectorId, externalId)` updates the previous device; `deviceId` can be omitted. If set, it must match the existing external identity. Row errors do not cancel other rows. `dryRun=true` saves only task and audit reports.

`GET /v1/devices:export?format=json&locationId=<id>` or `format=csv` returns devices in the allowed area. Export is limited to 100,000 devices and 64 MiB of JSON data. Pagination and export show current data as it’s read; for consistent snapshots, administrative changes should be paused during export.

Dry-run import checks the model, references, database permissions, and constraints by processing a row in a rollback savepoint. For such rows, no temporary catalog events are created or audit; the final task check has its own audit. Background transactions process up to 64 rows and atomically save position and report. This limits the time a lock is held and ensures resumption after restart.

## Floor Plans

`PUT /v1/locations/{id}/floor-plan?width=100&height=100` accepts binary PNG/JPEG and `If-Match` floor versions. Limits: 20 MiB, 16 million pixels, positive sizes in units. Metadata contains `fileId`, owner, site, version, size, type, SHA-256, and pixel sizes.

`GET /v1/locations/{id}/floor-plan` reads the current version; `?version=N` reads historical versions. `/floor-plan/versions` returns metadata pages. Each request rechecks the area. Bucket is not used as a public endpoint.

Failure to record an object, integrity check, or binding transaction leaves the previous version available. Background cleanup checks unbound reservations every 10 seconds after they expire. Previous bound versions are saved. The temporary directory contains only transferred bytes and is not a data source; unfinished transfers older than an hour are deleted upon startup.

## Maintenance

`POST /v1/maintenance-windows` accepts `deviceId` or `zoneId`, `startsAt`, required `endsAt`, `reason`, `responsible`, `suppression.eventTypes`, and/or `suppression.ruleIds`. Time is UTC. Policies list specific types or UUID rules. Empty policies and emergency types are not allowed.

The server sets status and version. A planned window becomes active, then completed. `PUT` changes an incomplete window with `If-Match`; the start of an already active window is preserved. `POST /v1/maintenance-windows/{id}:cancel` explicitly terminates it. Completed and canceled intervals are available for history and cannot be reopened.
Every change and automatic transition is recorded with auditing. The audit record contains the version number, a complete snapshot of the object, and the identifier of the message that triggered the change; the previous policy can be restored after compacting the configuration stream. The configuration stream also stores terminal versions. Consumers compare the time of event receipt with the set `activatedAt` and the end `endsAt`; when the window is cancelled, it is limited to `completedAt`. A window without `activatedAt` does not suppress signals. Old replays do not include retroactive maintenance suppression. Fire events and P1 pass regardless of the selected policy. The implementation of suppressing new signals belongs to the Correlation Engine; the catalog stores and distributes policies.

## Registration of connectors

The catalog receives complete versions of registrations from `psim.catalog.v1`. The trusted owner remains Gateway; the catalog does not read its tables. REST registration, rotation, and revocation send the original JWT through mTLS.

For registration of `certificatePem`, the parameter is sent with the body `Connector`. Rotation accepts `certificatePem` in the body, revocation takes the reason and the optional `fingerprint`. Without a certificate fingerprint, the connector is completely disconnected. These operations require the role `admin` or `integrator` and the area `*`. Gateway verifies the certificate and confirms the application of the change with live instances. If the outcome is uncertain, the same `Idempotency-Key` is repeated.

The dynamic fields `ConnectorStatus` are filled by the owner of the state projection; the current response from the catalog contains the registration. The connection status does not prove the freshness of device state.

## Deployment Requirements

On a single machine, the service uses common PostgreSQL, Kafka, and a private S3 bucket. In the cluster, two instances connect to one database and one bucket; import, processing, and outbox handlers are coordinated through PostgreSQL transactions and locks. Local content in the temporary directory is not required for restoration. For it, a private, writable directory with quotas sufficient for a limited number of simultaneous transfers is needed; the rest of the process’s file system can only be read.

The deployment configuration must define a separate database role for the `catalog` schema, S3 permissions only for the plans bucket, TLS trust, a client certificate for Gateway, and secure secret files. The external port serves HTTPS; runtime publishes internal `/health/live`, `/health/ready`, and `/metrics` on the administrative port. The backup includes PostgreSQL metadata and all associated object versions; during restoration, links, sizes, and SHA-256 are checked. Backup verification and clean restoration are performed according to the [restoration schedule](administration-and-recovery.md).
