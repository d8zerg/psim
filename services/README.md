# Services

<!-- language-links -->
[English](README.md) | [Русский](../docs/ru/repository/services/README.md)
<!-- /language-links -->

One directory per platform service (ADR-001): `application`, `adapters`, `main`. The service depends on its domain core from `libs/domain`, on `libs/platform`, and on generated contracts (`psim::contracts`), but not on other services.

The service is created from a template ([ADR-039](../docs/adr/0039-service-runtime.md)):

```
task service:new NAME=example-service CONTEXTS=incident SUMMARY="Example Incident Service"
```

The generator copies `tools/service-template` into `services/<name>` (executable file `psim-<name>`, DEB package, image `deploy/images/psim-<name>`) and registers the service in `tools/arch/architecture.yaml` along with its domain contexts. The service immediately receives configuration (YAML and `PSIM__*`), JSON logs, metrics `/metrics`, OTLP trace, `/health/live`, `/health/ready`, and a shutdown with draining; verification - `task test:observability` (FF-06). Connector Gateway, Normalizer, Correlation Engine, Incident Service, and API Gateway operate in [end-to-end test environment](../docs/engineering/event-flow.md).

Full normalization and loading of history are described in [guide](../docs/engineering/normalization-and-history.md). `task processing:test` checks deduplication and restoration from Kafka -> ClickHouse.

[Command Service](../docs/engineering/command-service.md) manages commands for devices, acknowledgments, deadlines, and results. `task commands:test` checks delivery through the actual gateway and SDK on two service instances.

[Response Engine](../docs/engineering/response-engine.md) executes scenario versions, saves transitions and timers, and sends commands to Command Service. `task response:test` checks restoration of two instances and replay without external effects.

[Notification Service](../docs/engineering/notifications.md) delivers notifications to the application, via SMTP with TLS and signed webhook. Response Engine manages SLA and duties. `task notifications:test` checks receipts, retries, shift changes, and process restoration.

[Audit Service](../docs/engineering/audit-service.md) builds a hash chain, signs external checkpoints, and provides search and export. `psim-audit-tool` performs migrations and independent verification. `task audit:test` checks Kafka history, two copies of the service, and database administrator intervention.

[Projection Service](../docs/engineering/incident-reports.md) loads incident facts and SLA raw values, provides reports and CSV. `psim-report-tool` recalculates facts from PostgreSQL. Verifications: `task reports:test`, `task reports:bench`.
