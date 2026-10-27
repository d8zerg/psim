# ADR-080. Telemetry storage and observability package

<!-- language-links -->
[English](0080-observability-package.md) | [Русский](../ru/adr/0080-observability-package.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-028](0028-clickhouse-analytical-storage.md), [ADR-035](0035-local-environment.md), [ADR-042](0042-operational-capabilities.md), [ADR-046](0046-s3-object-storage.md)

## Context

The delivery must include diagnostic tools without connecting to the development environment. Related metrics, logs, traces, alerts, and recovery instructions are required. The load of telemetry should not compete with event history queries and data recovery in ClickHouse.

## Options

| Option | Advantages | Limitations |
|---|---|---|
| Shared ClickHouse for history and telemetry | One storage mechanism, Apache-2.0 | Shared resources and failure domain; additional schemas, rights, and Grafana plugins for trace searching |
| Separate ClickHouse for telemetry | Isolation of load, Apache-2.0 | Additional ClickHouse instance; separate schemas and trace interface configuration |
| Loki and Tempo as separate processes | Native Grafana data sources, OTLP, log to trace transition, existing development experience | Additional processes and disks; AGPL-3.0 for standalone servers |

## Solution

1. The delivery uses Prometheus, Alertmanager, Grafana, Loki, Tempo, and OpenTelemetry Collector. Fixed images for standalone servers are used. These servers’ code is not linked to PSIM executable files. License policies for statically linked dependencies are maintained; the composition of standalone images is considered separately.
2. Logs are stored in Loki for seven days, traces in Tempo for three days. Prometheus stores metrics for up to fifteen days or when reaching 10 GiB. These are default parameters; disk capacity is selected based on actual traffic. Telemetry storage is separated from ClickHouse and working floor plan bucket.
3. Each node runs a Collector with a disk queue and diagnostic agent without administrative operations. Inter-node OTLP and metric collection use verified TLS; access to the agent requires a client certificate and authorized service name. Local interfaces of storage and Alertmanager listen on loopback.
4. The central package operates on the first node with role `observability`. Automatic switching of the observability node is not implemented. The external system must control the regularity of `PsimWatchdog`; local logs do not replace the external notification channel.
5. Grafana is accessible through `/monitoring/`. Access via Keycloak uses PKCE, ID token verification, and separate realm roles `psim-observability-admin` and `psim-observability-viewer`. Domain roles of individual tenants do not grant access to all infrastructure telemetry. The emergency local account receives a separate random password.
6. Dashboards, rules, source configurations, and HTML instructions are stored as ready-made files. English is the canonical source. Reviewed translation catalogs generate the checked-in Russian dashboards; both languages are shipped as ready-to-use files. Deployment does not generate their content. Rendering the topology uses addresses, identities, public URLs, and instance inventory. Tests read delivery materials directly.
7. An error in data collection or absence of data does not mean a correct state. The age of the last line in ClickHouse is evaluated together with Kafka lag. An end-to-end metric is not calculated by adding stage percentiles.

## Consequences

- The package uses the same application topology for Docker and systemd; Grafana receives the same screens.
- Backup monitoring accepts explicitly published results of completed tasks. Backup execution and recovery verification remain separate tasks.
- Matrix C-01-C-15 links failure with alert, dashboard, and instructions. Checking rules on time series does not replace cluster RPO/RTO qualification for fault tolerance.
- Schedule, access parameters, and check limits are provided in the [operating guide](../engineering/observability.md).

## Justifications

- [Notification configuration and Alertmanager suppression](https://prometheus.io/docs/alerting/latest/configuration/).
- [Tempo configuration](https://grafana.com/docs/tempo/latest/configuration/), [configuration structure of the fixed version](https://github.com/grafana/tempo/blob/v3.1.0/cmd/tempo/app/config.go).
- [Generic OAuth Grafana and PKCE](https://grafana.com/docs/grafana/latest/setup-grafana/configure-access/configure-authentication/generic-oauth/).
