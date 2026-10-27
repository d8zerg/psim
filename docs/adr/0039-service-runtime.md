# ADR-039. Service Runtime

<!-- language-links -->
[English](0039-service-runtime.md) | [Русский](../ru/adr/0039-service-runtime.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-001, ADR-005, ADR-016 (threading model), [ADR-040](0040-async-primitives.md) (shards and pools in `Context`), ADR-033, sections 5 and 6 of [crosscutting.md](../architecture/crosscutting.md#5-configuration), requirement SR-09, method FF-06

## Context

The unified framework for all services is a lifecycle with proper termination and draining, configuration from files and environment variables with validation according to the schema and partial hot reloading, `/health/live` and `/health/ready`, structured logs, metrics, and tracing through OpenTelemetry with Prometheus endpoint, OS signals, a unified error model with codes from the catalog; the result is the runtime library and service template. DoD: The service exports metrics, tracing, and health immediately from the template; termination does not lose processed messages.

Requirements: crosscutting.md - the service does not start if the configuration is invalid and reports all errors immediately; JSON logs in stdout with `trace_id`; `/metrics` on each service. SR-09 - secrets only from files with permission 0400, not from the environment or logs. The threading model and coroutines are defined in ADR-016 and ADR-040: Asio, shards, and a limited pool of blocking operations.

## Proposed Options

### Format and Configuration Schema

| Option | Pros | Cons |
|---|---|---|
| Protobuf messages as schema, JSON parsing | Types from one source with contracts | Parsing stops at the first error; no annotations for secrets and reloading |
| TOML + manual verification | Simple format | Schema in code, “all errors immediately” implemented manually |
| **YAML + JSON Schema (draft 7) with annotations `x-psim-secret`, `x-psim-reload`** | Declarative schema and documentation of settings; the validator (json-schema-validator) collects all errors; annotations describe secrets and reloading there too | Two libraries (yaml-cpp, nlohmann) |

### Metrics

| Option | Pros | Cons |
|---|---|---|
| OpenTelemetry SDK + Prometheus-export OTel | One API for metrics and tracing | The exporter pulls civetweb and opens its port; extra layer on the hot path |
| **prometheus-cpp (only core) + your `/metrics`** | Same port as health; fast counters; Prometheus format without conversions | Metrics do not go through OTLP (Collector takes them from Prometheus if needed) |

### Tracing and Logs

Tracing - **OpenTelemetry C++ SDK** with OTLP gRPC export (gRPC is already a dependency; HTTP-export and Zipkin are disabled to avoid using libcurl), sampling “parent or fraction of new traces”. Logs - **custom structured logger** (JSON strings on stdout): fields from crosscutting.md 6.3 and context of the active span; spdlog does not provide structured fields without string formatting, OTel SDK logs - extra path when Collector already reads stdout containers.

### Service HTTP

**Boost.Beast with Asio coroutines**, one thread - the same event loop as signals. Liveness is served by this same loop: if the loop hangs, the probe does not respond.

## Solution

1. **Libraries `libs/platform`:**
   - `errors` - `ErrorCode` and metadata (HTTP, gRPC, retry, header) are generated during build from `contracts/errors/errors.yaml`; `Error` (code + message without secrets), `Result<T>` = `std::expected<T, Error>`;
   - `log` - JSON strings `ts, level, service, instance, trace_id, span_id, msg` + fields; level changes on the fly;
   - `config` - YAML file, overrides `PSIM__<SECTION>__<KEY>` (type - from schema, unknown key - error), default values, all errors immediately; secrets - paths to files, read separately and not included in the configuration document, the file must belong to the service user and be inaccessible to groups and others, overriding secrets from the environment is prohibited (SR-09); the reload plan divides changes into those that apply and require restarting;
   - `observability` - `Metrics` (prometheus-cpp registry, text format 0.0.4), `Tracing` (OTLP gRPC, resource `service.name`, `service.version`, `service.instance.id`), `current_trace_ids()` for logs;
   - `runtime` - `Component` (`start`, `ready`, `drain`, `reconfigure` - Asio coroutines), `Runtime`, `run(argc, argv, ServiceDefinition)`.
2. **Service configuration:** runtime sections `service.instance` (default - host name), `admin.listen` (default `0.0.0.0:9100`), `log.level` (reloadable), `telemetry.otlp_endpoint`, `telemetry.sampling_ratio` (default 0.01), `shutdown.drain_timeout_ms` (default 30,000) and section `settings` with the service schema. Process options: `--config <file>`, `--check-config`, `--version`.
3. **Endpoints:** `/health/live`, `/health/ready` (all components are ready, startup completed, termination not started; otherwise 503 with component status), `/metrics`, `/info` (build and instance); only GET and HEAD. Runtime metrics: `psim_runtime_info`, `psim_runtime_ready`, `psim_runtime_start_time_seconds`, `psim_runtime_config_reloads_total{result}`, `psim_runtime_drain_timeouts_total`.
4. **Lifecycle:** Components are created by the service factory and start in order; startup error - exit code 1. SIGTERM or SIGINT: readiness is removed, components are drained in reverse order until the overall deadline; exceeding the deadline - code 3 and metric (work may have been lost), otherwise 0. SIGHUP: configuration is reread; invalid ones are completely rejected, changes to non-reloadable settings are reported with a warning, others are applied (`reconfigure`).
5. **Template and Generator:** `tools/service-template` - compilable service with a component example (receiving and processing messages with span and metrics, draining all started messages); `task service:new NAME=<name> CONTEXTS=<contexts>` creates `services/<name>` (executable `psim-<name>`, package, image) and registers the service in `tools/arch/architecture.yaml`, `services/CMakeLists.txt` and `IMAGES`. CMake function `psim_add_service`.
6. **FF-06** - `task test:observability`: Each executable service file is run in the local environment (Debian 13 x86_64); `/health/live`, `/health/ready`, `/metrics` are checked, JSON format of all log strings, tracing in Tempo through Collector, clean termination on SIGTERM. It is included in `env:ci-test`; the `env` stage of the pipeline is activated when runtime or services change.
7. **Clang-tidy Profile** (addendum to ADR-033): include-cleaner does not check headers for Boost, OpenTelemetry, prometheus-cpp, nlohmann, and yaml-cpp (it matches symbols with their internal headers); `modernize-use-designated-initializers` is disabled (partial initialization is caught by `-Wmissing-designated-field-initializers`), and rules with false positives in other code are disabled: `portability-template-virtual-member-function`, `clang-analyzer-core.CallAndMessage` (Asio coroutines; calls to null pointers are caught by `-Wnull-dereference`, ASan, and UBSan). After switching to the system libstdc++, the check for `cert-err58-cpp` is enabled.

## Consequences

- The new service receives configuration, logs, metrics, traces, health, and proper termination without its own code; its components only implement `start`, `drain`, and, if necessary, `ready` and `reconfigure`.
- Shards by CPU cores, blocking operations pools, and backpressure primitives are defined in ADR-016 and ADR-040, maintaining the interface `Component`.
- TLS for Collector and service HTTP is defined in SR-08 and ADR-043; JWT checks for service endpoints are not required; the port is not published outside the node’s network (SR-19).
- The tracing context is stored in a thread; when coroutines switch between `co_await`, it is explicitly passed by the component (propagation through the executor is defined in ADR-040).

## Verification

Unit tests for libraries and the template (including SIGTERM termination with code 0, exceeding the drain deadline with code 3, startup failure, rejection of invalid reloads, rejection of secret files with excessive permissions); `task test:observability` (FF-06) on local environment.
