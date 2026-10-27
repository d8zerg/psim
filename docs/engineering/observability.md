# Observability Package

<!-- language-links -->
[English](observability.md) | [Русский](../ru/engineering/observability.md)
<!-- /language-links -->

Status: Accepted. The decision on storage is [ADR-080](../adr/0080-observability-package.md).

The node agent is a separate program `psim-monitoring-agent` within `tools/monitoring-agent`, along with the package `psim-monitoring-agent` and the image `psim-platform/monitoring`. It runs from `--config /etc/psim-platform/monitoring/agent.json`, uses the monitoring UID, and has its own lifecycle. `psimctl diagnose` serves as a one-time administrative diagnostic. The general file input/output is located in `psim::platform_files`; protocol clients also come from the platform libraries.

## Composition and Access

The package is located in `deploy/observability/` and is part of the node configuration at `task deploy:render`. Images of the infrastructure from `deploy/topologies/images.json` and the image `psim-platform/monitoring:<version>` are required, along with the build done by `task deploy:images`. Dashboards, configurations, and instructions are provided as ready-made files; no separate generation is needed.

Grafana is accessed via `https://<public_name>/monitoring/`. Keycloak allows for separate realm roles `psim-observability-admin` and `psim-observability-viewer`: the first role gets Admin privileges, and the second gets Viewer privileges; both require MFA. The initial administrative account gets the first role at `task deploy:account`; further assignments are done separately in Keycloak. Product roles `admin`, `auditor`, and `operator` do not provide access on their own; telemetry contains aggregates from all provided resources. For emergency access, there’s `recovery-admin`; passwords are stored in the owner’s material at `grafana`, along with the file `admin.secret`.

For existing installations, explicit updates are required for inventory, material, monitoring accounts, and Keycloak clients. Regular sequential updates do not automatically add observability infrastructure. The initial installation imports the necessary roles and clients along with the realm.

Instructions are available via `/operations/runbooks/` through the same HTTPS entry. Each alert has a separate page containing diagnostics, recovery, confirmation of results, and access to the corresponding dashboard. There are no secrets or personal data on the pages.

English is the default language. Dashboard navigation switches between English and Russian while preserving the selected time range and filters. Alert links open instructions in the selected language; switching the instruction language preserves the current section. Both versions are included in the delivery.

## Dashboard Organization

| Screen | Main Function |
|---|---|
| Platform Overview | Critical alerts, unavailable instances, pipeline latency, flow, queues, and capacity |
| Event Pipeline | Stage throughput, p99 values for individual boundaries, lag by group and topic, new DLQ records, stopped partitions, and correlation restoration |
| Services and APIs | Readiness, traffic, duration, upstream errors, shared quotas, saturation, realtime, and related logs |
| Infrastructure and Replication | Availability of each process, response time, PostgreSQL and Valkey roles, replication, Keeper, etcd, HAProxy, and Keycloak |
| Storage and Capacity | Available bytes and inodes, growth forecast, ClickHouse load, parts, replication queue, and read-only replicas |
| Incidents and Connectors | Aggregates of incident states, active and disconnected connectors, sites without current duty, scenarios, commands, and notifications |
| Operational Checks | Backups, certificates, node clocks, diagnostic freshness, errors in telemetry and notification delivery |

All screens share the same time zone, browser settings, deployment filters, and transitions with saved filters and time. Service filters appear on screens with application metrics; infrastructure metrics remain tied to the node. Additional filters for groups, topics, components, and tenants appear on corresponding screens. Panels are grouped by tasks, charts have units, measurement boundaries descriptions, legend, and general hints. Detailed series are available through Grafana Explore. Missing measurements are displayed as `No data` (`Нет данных` in Russian); missing chart lines are not connected.

Colors do not replace numerical values. Disk thresholds are 15% and 5% of free space; certificates require renewal within less than 30 days. The initial window is one hour, updates are every 30 seconds. When working with failures, it’s necessary to record the interval, select a node, and transition from an alert to an instruction.

## Collection and Storage

The agent on each node checks the infrastructure, collects Linux resources, status of local PostgreSQL instances, Valkey, Sentinel, Keeper, ClickHouse, and metrics from administrative HTTP interfaces. Each node individually checks Kafka and PostgreSQL availability, allowing visibility into network segmentation. Detailed reading of Kafka lag and end offset of DLQ is performed by the central agent through Kafka Admin API.

The agent listens on port 19443, checks client certificates, and allows reading of metrics only for Prometheus and administrative identity. HAProxy can read HTML instructions, Alertmanager sends notifications. Proxy metrics only allow known services from the inventory, without arbitrary URLs. Failed downloads return an error. An outdated agent snapshot also makes scraping unsuccessful.

The PostgreSQL account has `pg_monitor` and only has access to fixed aggregate functions `monitoring.business_metrics()`. It does not have access to reading tables in the subject area. The function does not accept SQL or search parameters, uses a fixed `search_path` and time limit. Counts and technical identifiers of tenants are exported; incident texts, user names, and recipients are not exported. Missing current duty is considered based on the full catalog of sites, including sites without an assigned duty operator and those with only a backup recipient.

The agent receives copies of public certificates and trusted CA’s from its node. For PEM chains, the nearest expiration date is taken into account. When renewing certificates or changing trust, the public copy must be updated in the material at `monitoring`; this is part of the rotation rules. Private service keys remain with their owners. Data files collected by the agent only for reading capacity are retained by the original owner when creating node archives.

The application sends traces to the local Collector via TLS. Docker sends JSON logs through the local Fluent Forward with a limited asynchronous buffer. Native services write JSONL in `data/<node>/service-logs`; Collector reads files with position preservation. The native installation sets up logrotate and rotation configuration: size check is 20 MB, five compressed copies, copytruncate. The frequency of logrotate is chosen based on log intensity. It’s necessary to control the actual capacity of the data file.

Between the nodes of Collector, mTLS is used. The disk queue survives Collector restarts and repeats the sending if the storage is unavailable; it has a finite size. Logging and trace collection are not a persistent channel for event delivery. Logs contain `trace_id`, and the Loki source is connected to Tempo. Internal HTTP/gRPC ports of the storage are accessible only through loopback.

The end-to-end metric `psim_event_to_incident_seconds` measures the last originating event of a signal -> successful completion of incident recording. It excludes duplicate deliveries, timer signals with deadlines, and future timestamps. A separate counter `psim_incident_signals_persisted_total` counts unique recorded signals, including timers; transaction errors and repetitions do not increase its value. This metric includes rule waiting time and depends on the source time; it does not cover browser delivery. Console projection and realtime are measured separately. Per-stage p99 values must not be added to derive end-to-end p99. The load SLO retains separate verification.

## Backups

The central agent reads `data/<node>/monitoring/backups.json`. Backup tasks atomically replace this file after checking the result; it is not possible to update the successful timestamp at startup. Format:

```json
{
  "updated_at_seconds": 1000,
  "backups": {
    "postgres": {"last_success_seconds": 900, "size_bytes": 1024},
    "clickhouse": {"last_success_seconds": 900, "size_bytes": 1024},
    "kafka-config": {"last_success_seconds": 900, "size_bytes": 1024},
    "product-config": {"last_success_seconds": 900, "size_bytes": 1024},
    "plans": {"last_success_seconds": 900, "size_bytes": 1024},
    "identity-pki": {"last_success_seconds": 900, "size_bytes": 1024}
  }
}
```

The numbers in the example show the structure, not the final state of the test environment. The timestamp is set in Unix seconds, and the size in bytes. If there is no file, key, or valid value, the state is unknown, and the successful timestamp is zero. Backups older than 24 hours and states older than ten minutes have separate alerts. Intervals comply with backup policies. Backup and recovery qualifications are described in [the schedule](administration-and-recovery.md) and are not confirmed by this package.

## Notifications and Instructions

Prometheus sends alerts to Alertmanager. The initial configuration groups them by deployment, name, and node; critical receives the first delivery after ten seconds, warning after thirty seconds. When the disk is critically full, less urgent warnings for the same volume are suppressed. All alerts have public URL instructions and dashboards.

The regular recipient saves notifications in `data/<node>/monitoring/alerts.jsonl`, confirming the record with fsync. When 16 MiB is reached, one previous part is saved. This is a local delivery log. For the duty officer, an external recipient should be set in `alertmanager.yaml`: webhook, email, or other supported channel, with secrets in protected files. Changing the configuration file is accompanied by `amtool check-config` verification and controlled restart of Alertmanager. It is necessary to verify delivery to the real recipient.

`PsimWatchdog` is sent every minute. Its absence should be detected by an external system, independent of the observability node. The local log will not detect its own unavailability.

`PsimSignalInterrupted` compares actual Kafka publishing with Incident Service processing. A normal event flow without rule violations does not trigger this alert. A two-minute alert window does not replace the five-second pause limit during updates.

## Tests and Limits

- `task deploy:observability:check` checks navigation, units, no overlapping panels, all PromQL, alerts transitions on test series, C-01-C-15 links, storage configurations, and access limits to business aggregates in PostgreSQL.
- `task deploy:observability:test` additionally runs isolated Compose, checks provisioned Grafana and its sources, mTLS, real Kafka protocols, PostgreSQL, Valkey, Keeper, and ClickHouse, lag in two records and DLQ, writing and reading OTLP logs and traces, container and native process JSON logs, and Prometheus -> Alertmanager -> log delivery. Chromium opens fourteen EN/RU dashboards, scrolls all panels, checks queries and horizontal overflow, saves snapshots.
- `task deploy:check` checks profiles of one machine and cluster, process isolation, and Helm rendering. Kubernetes does not receive separate observability qualification.

Artifacts - `build/observability/smoke-result.json`, `smoke-containers.log`, and `screenshots/`. Test keys and containers are removed after running. The matrix `deploy/observability/chaos-coverage.json` confirms the existence of a diagnostic path for each failure; RPO/RTO values and LOAD-50K performance are confirmed by separate cluster qualifications. Live testing of all infrastructure protocols and Grafana access through Keycloak on E-CLUSTER remains part of operational acceptance.
