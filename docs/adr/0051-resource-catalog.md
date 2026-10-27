# ADR-051. Resource Catalog: Resources, files and maintenance

<!-- language-links -->
[English](0051-resource-catalog.md) | [Русский](../ru/adr/0051-resource-catalog.md)
<!-- /language-links -->

- Status: Accepted
- Related decisions: ADR-008, ADR-026, ADR-042, ADR-046, ADR-049

## Context

The catalog must store the hierarchy of objects, device types, devices, and sources, publish agreed versions, and serve floor plans. A failure in S3 or the process must not disrupt the available plan. Maintenance deadlines must be maintained independent of the process.

## Solution

1. `psim-resource-catalog` uses platform runtime, HTTPS, and verified JWT. Tenants are retrieved from `psim_tenant`, rights from client roles `psim`. `psim_scopes` contains UUIDs of allowed sites; `*` represents the entire tenant. Empty scopes do not provide access to resources. Resource management requires the role `admin`; general device types and connection management require the tenant-wide scope. The operator can read resources from its site.
2. The service owns the schema `catalog`. Changes to resources, their versions, audit records, results of idempotent requests, and outboxes are recorded in one transaction. Kafka records contain a full snapshot and version. Audit keeps snapshots of each version and a link to the message that created it, including intermediate maintenance policies. Archiving stores snapshots with `RESOURCE_STATE_ARCHIVED`. PostgreSQL library relays with possible repetitions; consumers compare versions.
3. Changes to hierarchy and associations are serialized by the tenant’s transaction advisory lock. This preserves checks on parent objects, uniqueness of sources, coordinates, and empty locations in multiple instances. Import commits small batches, freeing up locks between them. Horizontal scaling of the catalog is limited by this serialization; the service does not process each event.
4. The existing trust registry of the Gateway remains the owner of certificates and their revocation. The REST catalog delegates operations through mTLS with identity `psim://service/resource-catalog` and original JWT. The Gateway publishes `CatalogRecord.connector` from its outbox. Unpublished registrations are transferred to the stream upon startup, with version retention. The catalog builds its own copy; it does not need access to schema `ingest`. The catalog publishes other `CatalogRecord` variants.
5. Import tasks, input rows, and progress are stored in PostgreSQL. Authorization is recorded when a task is accepted, errors are returned per row. External device identity is `(connector_id, external_id)`; reimport updates the previous device. Duplicates within a file are rejected. Packet transactions and savepoints allow import to continue after an instance fails. `dryRun` rolls back resource changes and their outboxes, retaining audit reports.
6. Port `FileStore` separates the API catalog from S3. Images are fully decoded using libpng or libjpeg-turbo, limited to 20 MB and 16 million pixels. SVG and type substitution are rejected. Decoding and file input/output are performed in a limited BlockingPool.
7. Before S3 recording, the catalog creates a durable reservation of unique keys. PUT creates an immutable object; SHA-256 and size are verified. Then, metadata, new plan version, and floor version are linked with verification `If-Match`. Errors retain previous links. Historical versions are available through the same access scope checks. Unbound reservations expire after 10 minutes; background cleanup removes objects and then records. Deletion blocks simultaneous binding. The bucket is private, temporary public links are not provided.
8. `MaintenanceWindow` stores a device or zone, site, UTC interval with mandatory expiration, reason, responsible person, explicit policy, and version. The initial state is set by the server. Terminal windows are not reopened. Transitions `scheduled -> active -> completed` and cancellations are recorded with audit and published in `psim.config.v1` by key `maintenance/<tenant_id>/<maintenance_id>`. The scheduler polls every 100 ms; during startup, overdue state transitions are recovered until REST is opened.
9. Fire types and wildcards are rejected in the suppression list. The consumer contract also prohibits suppressing any fire events and P1, regardless of selected rules. Applying policies in the Correlation Engine does not change history or open incidents. The consumer considers stored time boundaries based on event acceptance time, not when the old configuration record arrives.

## Consequences

For delivery, PostgreSQL, Kafka, Schema Registry, Keycloak, a private S3 bucket, and trust in the Gateway are required. Two instances use the same database and bucket. Repeated tasks do not require local service files. Old plan versions are intentionally retained; policies for their deletion and consistent metadata backup with objects are part of operational procedures.

REST boundaries, files and maintenance expand with additional fields and methods. Existing fields and numerical identifiers are retained. User interface capabilities, normalization with enrichment, and suppression applications are developed separately.

## Verification

`task catalog:test`, `task catalog:bench`, `task catalog:sanitizers`, `task catalog:coverage`, `task contracts:check`. Load testing compares identifiers of all devices between input, REST export, and current Kafka consumer; measures import time and publication delay. File tests inject S3 and metadata write failures, verify versions, access scope and cleanup. Maintenance is tested with two instances and after restarting them.
