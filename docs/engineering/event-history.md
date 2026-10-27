# Event History Search and Incident Links

<!-- language-links -->
[English](event-history.md) | [Русский](../ru/engineering/event-history.md)
<!-- /language-links -->

Event History stores events, links to signals and incidents, as well as extending the storage of related data. Decisions: [ADR-054](../adr/0054-event-history-links.md), [ADR-028](../adr/0028-clickhouse-analytical-storage.md), [ADR-052](../adr/0052-normalization-and-event-history.md).

## Launching

The service uses previous PostgreSQL, Kafka, and ClickHouse settings. `history_api: true` includes an HTTPS API; the loader profile without the API maintains compatibility with the previous test environment. `listen_port` sets the port. `tls_certificate_file`, `tls_private_key_file`, `tls_ca_file`, `jwks_ca_file`, `issuer`, and `jwks_url` set TLS and JWT verification. The local test uses the certificate `history-server`; the delivery sets explicit files.

`history_batch_messages` limits the input batch (default 5000, maximum 50,000); the prepared block limit remains 8 MiB. `history_batch_wait_ms` limits the batch accumulation time (default 100 ms, maximum 1000 ms). The load release profile uses four instances, with a maximum of 50,000 messages per batch, a window of 500 ms, and eight blocking threads per instance. The failure checks use two instances.

`history_read_user` and `history_read_password_file` allow only SELECT queries from ClickHouse to read the API. Without them, a connection configured for loading is used. Secrets are stored in file 0400, belonging to the process user. Keycloak network requests are not executed for each row: the API applies a verified token scope through `libs/platform/authz`.

## API

- `GET /v1/events` - events within the required interval `from` and `to`, with new ones coming first. The maximum search interval is 31 days. Filters: repeated `siteIds`, `zoneIds`, `deviceIds`, `types`, `minSeverity`, `significantOnly`. The object is defined by the site `siteIds`; searching by hierarchy of buildings and floors requires selecting the catalog zones in advance. The type supports exact values and prefixes ending with `.*`.
- `GET /v1/events/{event_id}` - one available event. Optional `from` and `to` specify the interval; default is the last 90 days. An unknown or unavailable identifier within the scope and interval returns 404.
- `GET /v1/incidents/{incident_id}/events` - events through all incident signals. `unavailableEventIds` contains links without available bodies. Access checks apply to both incidents and source events; the link does not expand access to another zone.
- `GET /v1/events/rule-statistics` - matches by rule_id and immutable rule_version within a required interval of up to 31 days. Additional filters include sites, zones, and ruleIds. `signalCount` counts logically unique signals, `eventReferenceCount` counts unique evidence for each signal, not globally unique events.

The default page size is 100, with a maximum of 1000. `nextPageToken` is passed as `pageToken` with previous filters. The cursor does not apply to another scope or query. Searching is ordered by time and ID; incident links are ordered by source event ID, statistics by rule ID and version. Pagination does not freeze the entire dataset during continuous recording.

History is read by operators, supervisors, and auditors. Statistics of rule settings are read by supervisors, integrators, admins, and auditors. Tenant and scope are required; an empty scope returns an empty set. UUIDs, filters, dates, and cursors are validated. Queries have size and execution time limits.

## Storage and Recovery

`events` stores the complete normalized stream. `history_facts` contains signals and data related to incident events; views `signal_facts`, `event_signals`, and `incident_signals` read it with `FINAL`. This is an analytical projection, not the source of truth for Incident Service. Incident summary statistics are described in the [reporting guide](incident-reports.md).

The signal extends its original events according to the significant-event retention policy. The queue `history.retentions` stores requests until the write to `retained_events` is confirmed; out-of-order delivery does not lose requests. Reinsertion after SIGKILL does not change the logical result. When reading, the original and extended copies are combined; repeated events and links do not appear in the API. TTL deletes both categories independently. After physical deletion of the source, the link alone cannot restore the body; access to the original log or backup is required.

## Tests

```sh
task history:test
task history:sanitizers
task history:coverage
task history:bench
```

Reports are stored in `build/event-history/<preset>/`, and the load report in `build/event-history/release-bench/`. The functional test checks two processes, reverse order of events and signals, access scopes, pagination, TTL, ClickHouse shutdown, and SIGKILL loaders. Complete sets of link identifiers are compared. The release measurement separately checks search within a day during recording, with at least 50,000 events per second; evaluation includes TLS, JWT verification, ClickHouse, and response serialization. The load measurement is run after other pipelines on the host finish. The full long-term cluster load remains in delivery checks.
