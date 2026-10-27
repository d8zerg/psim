# Incident Reports

<!-- language-links -->
[English](incident-reports.md) | [Русский](../ru/engineering/incident-reports.md)
<!-- /language-links -->

The Projection Service serves `ReportService` and loads incident facts into ClickHouse. The semantics of metrics and storage model are defined in [ADR-057](../adr/0057-incident-reporting.md).

## Launch and Access

Each process specifies a non-empty `service.instance`. The service `psim-projection-service` uses a shared stream runtime: Kafka, Schema Registry, PostgreSQL for batch journal and audit outbox, TLS API, JWT/JWKS, readiness, metrics, and tracing. First, the Incident Service and Response Engine are launched. Their PostgreSQL schemas are needed for recalculation; the regular loader reads from Kafka.

| Configuration of `settings` | Assignment |
|---|---|
| `clickhouse_host`, `clickhouse_port`, `clickhouse_cluster` | Node, native port, DDL cluster |
| `clickhouse_user`, `clickhouse_password_file` | Role of loader and protected password file |
| `clickhouse_ca_file`, `clickhouse_certificate_file`, `clickhouse_private_key_file` | TLS ClickHouse |
| `reports_read_user`, `reports_read_password_file` | Dedicated role for API with only SELECT for view and its dependencies |
| `reports_batch_messages`, `reports_batch_wait_ms` | Batch size and delay, default 5000 and 100 ms |
| `tls_certificate_file`, `tls_private_key_file`, `tls_ca_file` | TLS API, default `reports-server.pem` and `reports-server.key` from `tls_directory` |
| `issuer`, `jwks_url`, `jwks_ca_file` | Identity verification |

Consumer group - `psim-projection-service`, start of reading - earliest in available retention. Local unencrypted connection is allowed only with explicit `allow_plaintext`. The loader creates tables and views on the cluster, so DDL rights are needed at initial launch. Production delivery should separate installation privileges from runtime privileges. The API can use a separate reading role. The API user receives roles `manager`, `supervisor` or `auditor` and a site or zone scope. The tenant is taken from verified JWT.

## API

- `GET /v1/reports/incidents` - JSON as Protobuf JSON mapping.
- `GET /v1/reports/incidents:export` - the same report in CSV with header and summary line.

Required parameters: `filter.from` and `filter.to` in RFC 3339, with precision no finer than milliseconds. The interval `[from,to)` is not longer than 400 days. Additional repeated parameters: `filter.site_ids`, `filter.incident_type_ids`, `filter.priorities`. Names `filter.siteIds` and `filter.incidentTypeIds` are also accepted. For each filter, no more than 100 values are allowed. Priorities: `PRIORITY_P1`-`PRIORITY_P4`.

`grouping` accepts `REPORT_GROUPING_NONE`, `REPORT_GROUPING_SITE`, `REPORT_GROUPING_INCIDENT_TYPE`, `REPORT_GROUPING_PRIORITY`, `REPORT_GROUPING_OPERATOR`, `REPORT_GROUPING_DAY`. By default, totals are returned. The day is determined in UTC. The priority group contains numbers 1-4, the operator group - the identifier of the last responsible person; an empty group indicates no assignment. Limit - 10000 groups, exceeding returns an error without truncation.

`incidentsCreated` - number of created incidents during the period, `incidentsResolved` - number of resolved incidents during the period, including those created earlier. MTTA relates to acknowledgement during the period, MTTR and false alarms - to resolution during the period. When grouping by days, the time of the corresponding observation is used. SLA relates to the first moment of completion and deadline; unfinished observations are considered after the deadline. `mtta` and `mttr` - durations, `falseAlarmRate`, `ackSlaCompliance` and `resolveSlaCompliance` - fractions. `ackSlaEvaluated` and `resolveSlaEvaluated` allow distinguishing zero fraction from no observations. `escalated` counts incidents created during the period with a non-zero current escalation level. The operator load is represented by `incidentsCreated` when grouped by the last responsible person. Results are calculated based on original observations. If there are no data, counters and fractions are zero; zero fields may be absent in JSON according to Protobuf mapping.

CSV uses UTF-8, comma, CRLF, and double quotes. Column `row_kind` distinguishes summaries and groups. Values starting with symbols get apostrophes for safe opening in a spreadsheet editor. Answers have `Cache-Control: no-store`.

## Recalculation

`psim-report-tool` is included in the package and image of the Projection Service:

```sh
psim-report-tool rebuild TENANT_UUID FROM_MS TO_MS
```

The recalculation boundaries are chosen based on the time of creation of incidents and are set in Unix time in milliseconds. To restore reports, incidents created before the reporting period are also needed; when restoring completely, the entire available range is selected. PostgreSQL: `PSIM_PG_HOST`, `PSIM_PG_PORT`, `PSIM_PG_DATABASE`, `PSIM_PG_USER`, `PSIM_PG_PASSWORD`, `PSIM_PG_SSLMODE`, `PSIM_PG_SSLROOTCERT`, `PSIM_PG_SSLCERT`, `PSIM_PG_SSLKEY`. By default, TLS checks the hostname and CA. The password is sent in a secure environment, not as an argument to the command. Roles require only SELECT for `incident.incidents` and `escalation.incidents`.

ClickHouse: `PSIM_CH_HOST`, `PSIM_CH_PORT`, `PSIM_CH_USER`, `PSIM_CH_PASSWORD_FILE`, `PSIM_CH_CA_FILE`, `PSIM_CH_CERT_FILE`, `PSIM_CH_KEY_FILE`. Only for isolated local test environment, `PSIM_CH_ALLOW_PLAINTEXT=1` is allowed. The recalculation role gets INSERT for `incident_facts`; the materialized view must already exist.

The utility reads a consistent snapshot in portions of 200 incidents. If there’s an error, part of the facts may be inserted; repetition is safe. The live loader doesn’t need to be stopped. To verify restoration, reports are compared with `incident_report_current` before and after recalculation on stopped sources. Events before the available retention in Kafka are restored from PostgreSQL.

Don’t delete only `incident_report_states`, leaving batch journal and loader deduplication tokens as the only restoration mechanism. Perform PostgreSQL recalculation after restoring the schema. The utility supplements the projection, but doesn’t delete incorrectly inserted rows or versions.

## Operation and Testing

`psim_clickhouse_*` shows batches, errors, and loader delay; `psim_report_query_seconds` - API processing duration, including audit. Unfinished batches are stored in `psim_clickhouse.pending_batches`; errors repeat with delay. Readiness worsens when the loader has an error, a background task stops, or the audit publication fails. When the delay increases, check Kafka lag, PostgreSQL and ClickHouse availability, batch size, and table parts. If the audit publication fails, check `reports.psim_outbox` outbox and access to `psim.audit.v1`.

```sh
task reports:test
task reports:bench
task reports:sanitizers
task reports:coverage
```

The results are stored in `build/reports/<preset>/report.json`, and the logs are located nearby. The volume and results of the load measurement are indicated in the report; a limit of three seconds is checked for all five requests, including the first one.

The local Prometheus loads the rules for delay reporting and projection errors. Grafana provisions the dashboard `PSIM incident reports`: p99 API, loading errors, insertion speed, and readiness. For another topology, the detection of administrative port addresses for the service is configured.

A synthetic set of one million incidents over 30 days was tested: five requests took 1.76-2.23 seconds. This is a test of the report at a specified volume, without simultaneous event flow load. The full load is checked during system qualification. Integration tests were conducted in Debug, ASan/UBSan, and TSan modes, including an incident created before the period began, repetitions, reopening, handover, SLA boundaries, access by managers, and restoration of the materialized state. Service coverage is 89.37% of lines at a threshold of 75%. The DEB package and container contain the service and a utility for recalculation.

## Co-location with console read models

The Projection Service also owns [console read models](console-projections.md). Flags `reports_enabled`, `console_enabled`, and `console_writer` allow separating the report loader, the single console writer, and reading instances. For an earlier separate reporting configuration, set `console_enabled: false`. The package and service image contain both utilities: `psim-report-tool` and `psim-console-tool`.
