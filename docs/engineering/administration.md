# Administration

<!-- language-links -->
[English](administration.md) | [Русский](../ru/engineering/administration.md)
<!-- /language-links -->

The administrator configures the system through `/admin`. The integrator manages connectors and mappings through `/integrations`. The current OIDC session, public REST API Gateway, and access scope checks are used. The solution is [ADR-072](../adr/0072-administration.md).

## Catalog

Select a section and create a record or open an existing one. The form provides a UUID; it can be replaced before the first save, for example, to ensure identity consistency with external PKI certificates. After saving, the identifier cannot be changed. The server assigns a version and metadata. Lists support filtering and sequential pages; background reading updates the list while preserving editable fields.

Hierarchy: site -> building -> floor -> zone. For a site, specify the IANA time zone; for other locations, specify the parent. Changing the location type removes incompatible time zone or parent values. The site is calculated by the server. The device connects to the zone, type, and connector; sources include an external link and kind. Resource status is set separately from connection quality. Registering a connector does not confirm the device state freshness.

Device types, mappings, rules, incident types, SLA, and settings require tenant-wide scope. Response scenarios are also common definitions. The site administrator manages allowed locations, devices, files, and maintenance. An unavailable general configuration is not requested.

## Import and Floor Plans

In the import section, select a CSV with headers or a JSON array of devices and perform checks. The interface shows saved tasks, counters, and error messages. Application is available after successful checks without errors for the same file. Selecting another file cancels previous results. Limit is 64 MiB. Unknown write outcomes require an exact retry; automatic retry is not available.

In the plan section, specify the UUID of the floor. PNG/JPEG upload uses the current floor version and positive floor plan dimensions. Extension/type, size up to 20 MiB, and decoding up to 16 million pixels are checked; the server rechecks the content. Versions are read page by page. The view receives the file through an authorized API and releases the Blob URL after closing or changing the version. There are no public URLs in the storage.

## Normalization, Rules, and Scenarios

For a new connector type, create a mapping set with the same `connectorType`, taxonomy version, and ordered rules. Each rule contains source code or a prefix with the ending `*`, optional CEL condition, target taxonomy type, and attribute mappings. Publishing saves the set version with a check against the previous version.

Incident type sets priority, grouping, response requirement, and reference to escalation policy. The policy includes acknowledgement and resolution times for priorities, levels with increasing delays, recipients, and channels.

Correlation rules are edited in fields: filter, scope, window, allowed lateness, suppression, one type of correlation, and output signal. First save a draft, then check the saved version. A trial run accepts UTC range and Kafka or ClickHouse source. Its status and counters are read until completion. Activation after confirmation preserves the other rules and uses the version of the active set. Server returns errors for range, CEL, and reference errors.

A scenario sets bindings to incident types, initial step, and list of steps. Step type is explicitly selected: instruction, checklist, decision, command, notification, waiting, escalation, or external handler. Nested lists, branches, and parameters are filled with separate fields. Selecting another type removes previous conflicting parameters. Check and publish apply to the saved version; changing the form requires saving and rechecking. Archiving requires confirmation.

## Maintenance and Certificates

The maintenance window contains a device or zone, start, required end, reason, responsible person, and specific event types or UUID of suppression rules. The server records automatic transitions and audits. To terminate early, enter the reason and confirm the action. Fire incidents and P1 are not suppressed.

To register a connector, specify its UUID, type, and public PEM issued by external PKI for this identity. Renewal supplies a new public certificate. The private key remains outside the browser. Fully disabling a connector requires a reason and confirmation. Certificate registry and registration status are available in the card; all changes go through the existing Gateway audit and catalog.

## Access and Errors

The right-hand panel reads `/v1/me`: current user, tenant, roles, sites, zones, and allowed actions. Assigning roles `psim` and `psim_scopes` is done in the Keycloak console of the selected realm. It requires a separate identity administrator session. The link does not pass application bearer tokens; the site administrator does not get authority to manage IdP. After changes, update the panel; current server checks apply to active sessions.

Version conflicts retain entered fields. Explicit rereading requires confirmation to undo changes. Unknown outcomes block new writes until an exact retry with the previous key. In case of network failure, failure to read current state, exercises, and video wall, writes are prohibited. Authentication failure hides previously loaded data. Changing identity cancels requests and clears cache.

## Checks

```sh
task web:check
task admin:test PRESET=release
task response:test PRESET=release
```

Module checks cover form structures, oneof, saving entered fields, versions, retries, revoked permissions, import, and files. The native Playwright run uses real PKCE with MFA, HTTPS, Kafka, PostgreSQL, Gateway, and private file storage. S1/S2 configuration and rules with scenarios are created by browser actions. Then, a new mTLS identity is checked through normalization and correlation before incident and published scenario execution. A separate site administrator receives errors when reading another floor.

Diagnostics, screenshots, and reports are in `build/admin/<preset>/`. The Compose environment created by the check is deleted; `PSIM_ADMIN_KEEP_ENV=1` saves it for diagnosis. Checking the existing environment does not delete its files. Bundle size - [comparison of deliveries](administration-assets.md); the full graph’s limit is accepted in [ADR-073](../adr/0073-administration-delivery-budget.md).
