# Cross-cutting Concepts

<!-- language-links -->
[English](crosscutting.md) | [Русский](../ru/architecture/crosscutting.md)
<!-- /language-links -->

Section 8 arc42. Concepts are implemented in the platform libraries and are the same across all services.

---

## 1. Message Envelope

The value of each Kafka message is a record from the `<Topic>Record { Envelope envelope = 1; oneof payload {...} }` topic ([ADR-004](../adr/0004-serialization-and-schema-evolution.md)). The payload type is determined by the `oneof` branch. The `psim.common.v1.Envelope` envelope ([ADR-005](../adr/0005-envelope-and-identifiers.md)):

| Field | Type | Purpose |
|---|---|---|
| `message_id` | UUID | Message identifier: `event_id`, `signal_id`, domain event identifier |
| `tenant_id` | UUID | Tenant |
| `occurred_at` | timestamp (μs) | Time of occurrence in the subject domain |
| `produced_at` | timestamp (μs) | Time of recording in Kafka |
| `received_at` | timestamp (μs) | Time of receipt by the gateway (for input events) |
| `sequence` | integer | Source sequence number (for events) or version of the aggregate (for domain events) |
| `causation_id`, `correlation_id` | UUID | Cause of the message and end-to-end chain from the original event |
| `actor` | structure | Subject: user, service, connector, or system |
| `schema_version` | string | Contract version used to produce the message |

Kafka headers: `traceparent`, `tracestate` (W3C Trace Context), `psim-ts-<stage>`, DLQ headers. The wire format is Confluent wire format: magic byte, schema identifier, Protobuf message indices, message body.

## 2. Identifiers and Time

- Identifiers follow the [aggregates.md](../domain/aggregates.md#01-identifiers) rules.
- The domain core receives time only through port `Clock`, identifiers through `IdGenerator`, randomness through `Random`. In simulation, these ports are deterministic.
- All times are stored and transmitted in UTC with precision to the microsecond. The time zone is only applied when displaying and reporting (according to `Site.timezone`).
- Time mismatch of sources is detected by comparing `occurred_at` and `received_at` (RE2) and reflected in the metric and event `system.source.clock_skew`. Platform nodes are synchronized with chrony; node mismatches are controlled by the metric.

## 3. Transaction Patterns

### 3.1 Outbox

```
BEGIN;
  UPDATE/INSERT aggregate ... WHERE version = :expected;
  INSERT INTO outbox(outbox_id, aggregate_id, topic, key, envelope, created_at);
COMMIT;
```

Outbox relay:

- Polls through the clock abstraction, waiting 100 ms by default when idle or after a retryable error;
- Selects pending rows with `SELECT ... FOR UPDATE` in `sequence` order;
- Publishes through an idempotent producer, waits for acknowledgement, and marks rows as published;
- Uses a transaction advisory lock per schema so that concurrent instances elect one publisher and preserve publication order.

### 3.2 Inbox

```
BEGIN;
  INSERT INTO inbox(message_id, received_at) ON CONFLICT DO NOTHING;
  -- If no row was inserted: duplicate, skip
  aggregate changes + outbox;
COMMIT;
-- After the batch commits: commit the Kafka offset
```

Inbox records are deleted after retention of the input topic has been doubled.

### 3.3 Kafka Transactions (EOS-K)

One transaction producer per shard (`transactional.id = <service>-<instance>-<shard>`): input offsets are committed in the transaction with consumer group metadata (KIP-447), so zombie instances are eliminated by the group generation after rebalancing. The commit interval is time-based (≤ 50 ms) or batch size. Details - ADR-006 and [ADR-041](../adr/0041-kafka-layer.md), which clarifies the mechanism (instead of producer per input partition).

## 4. Error Model

- The error code catalog is unified for REST, gRPC, and DLQ: `<DOMAIN>_<REASON>`, for example `INCIDENT_INVALID_STATE_TRANSITION`, `CATALOG_DUPLICATE_EXTERNAL_ID`, `AUTH_SCOPE_DENIED`. The catalog is part of the contracts.
- REST returns `application/problem+json` according to RFC 9457: `type` (URI code), `title`, `status`, `detail`, `instance`, `code`, `trace_id`, `errors[]` for validation errors.
- The connector gRPC protocol uses standard statuses and `google.rpc.ErrorInfo` with the same code.
- The domain core returns errors as values (`std::expected<T, DomainError>`); exceptions are allowed only at the infrastructure boundary for unexpected situations and are converted to code `COMMON_INTERNAL`.

Error handling class for message processing:

| Class | Example | Behavior |
|---|---|---|
| Transient | PostgreSQL unavailable, Kafka timeout | Partition pause, retry with exponential backoff; order is preserved |
| Permanent for the message | Unknown device, invalid payload | DLQ, continuation |
| Program error | Violation of invariants, unhandled exception | Message in DLQ, metric and alert; if repeated in multiple messages - circuit breaker stops the partition to avoid sending the entire stream to DLQ |

## 5. Configuration

| Level | Source | Change |
|---|---|---|
| Static service | YAML file + environment variables `PSIM_*` (override the file); check against JSON Schema at startup | Restart; some parameters (log level, limits) - hot reloading on `SIGHUP` signal |
| Secrets | Regular files owned by the service user with permissions `0400`; secret agents materialize these files; in configuration - only file references | Restart or hot reloading of certificates |
| Domain (rules, mappings, retention) | `psim.config.v1` | No restart, atomic |
| Reference (catalog) | `psim.catalog.v1` | No restart |

The service does not start if the configuration is invalid and reports all errors immediately.

## 6. Observability

### 6.1 Tracing

- OpenTelemetry SDK for C++, export OTLP to OpenTelemetry Collector.
- Context is transmitted through Kafka headers (`traceparent`), gRPC metadata, and HTTP headers. The connector starts tracing; if it hasn't started, the gateway does.
- Batch processing: a batch span with links to contexts of incoming messages; span of the outgoing message - child of the incoming span. Thus, one trace goes from the sensor to the operator action.
- Head sampling on the hot path is configured by probability (default 1%). Tail sampling cannot recover traces already excluded by head sampling; retaining every signal or incident trace requires an appropriate collection policy and separate validation.

### 6.2 Metrics

- Prometheus endpoint `/metrics` on each service; names `psim_<subsystem>_<metric>_<unit>`.
- Service metrics: latency, throughput, errors, saturation (consumer lag, queue filling, connection pool).
- Business metrics: events by taxonomy, percentage `generic.*`, signals by rules, incidents by type and priority, MTTA and MTTR in a sliding window, SLA violations, commands by terminal status.
- Pipeline metrics: histograms of stages and end-to-end latency ([quality.md](quality.md#44-how-it-is-measured)).

### 6.3 Logs

- Structured JSON lines in stdout: `ts`, `level`, `service`, `instance`, `trace_id`, `span_id`, `tenant_id`, `msg`, event fields. Collected by the Collector.
- During the hot path, individual events are not logged at a level above `debug`; errors are aggregated with frequency limits.
- Personal data (access-control card identifiers) is not logged.

### 6.4 Health
- `/health/live` - The process is alive and the event loop responds.
- `/health/ready` - Dependencies are available, local catalog replicas and configuration have been fully read, assigned partitions have been restored.
- Stop: `ready = false` -> Stopping reception -> Draining: completing current batches and transactions, committing offsets, closing the producer -> Exit. The drain timeout can be adjusted.

## 7. Security
Overview. Threat model - [threat-model.md](../security/threat-model.md); threats, security requirements `SR-NN` and responsible components - [threat-register.md](../security/threat-register.md).
| Boundary | Mechanism |
|---|---|
| Connector -> Gateway | TLS 1.3, mTLS; connector certificate is linked to registration in the catalog (C1, C2); limits on connectors |
| Browser -> API Gateway | HTTPS; OIDC Authorization Code + PKCE; JWT access with short duration; WebSocket authentication with token upon connection and reauthentication when the token is updated |
| Service -> Service | mTLS with internal CA; synchronous calls carry the user’s JWT (delegation) or service token client credentials; each service verifies the token and permissions itself |
| Service -> Kafka, PostgreSQL, Redis | TLS; separate account for services; ACL in Kafka by topic; PostgreSQL permissions - only its own schema |
| Access decisions | Common policy library `decide()` (section 6 [aggregates.md](../domain/aggregates.md#6-identity--access)); deny by default |
| Audit | Every mutation and every access denial |

## 8. Multitenancy
- `tenant_id` exists in each entity, each message, each cache key and index in PostgreSQL (the first column of composite indexes).
- A tenant is determined from the token (user), connector registration (events) or envelope (messages), never from request parameters.
- The MVP operates one tenant. Authorization tests must still reject access across tenant boundaries, and the invariant “no queries without `tenant_id`” is checked by a fitness function. ADR-022.

## 9. Realtime: Snapshot and Deltas
1. The client requests a snapshot of the feed: `GET /v1/feed/incidents`. The Projection Service returns incidents within the access scope and offset vectors `psim.incidents.events.v1` and `psim.response.events.v1`, on which the snapshot is aligned.
2. The client opens a WebSocket and subscribes to this vector as a resume token.
3. API Gateway reads topics with the specified offsets, filters by access scope and subscription, and sends deltas. The client applies the delta only if `version` is greater than the known version.
4. After a break, the client reconnects with the last received offset vector and receives the missed data. If the offsets exceed the retention period or the break is too long, the server responds with `REALTIME_RESYNC_REQUIRED`, and the client gets a new snapshot.
5. Slow clients: The sending queue is limited; when it’s full, the connection closes with code `REALTIME_SLOW_CONSUMER`, and the client recovers as described in step 4.

Each instance of API Gateway reads topics completely (without consumer groups) and maintains a common stream for all connections; the volume of incident events is orders of magnitude lower than that of sensor events. ADR-019.

Clarification of realtime: [ADR-060](../adr/0060-realtime-delivery.md) defines node-independent resumption, type negotiation and the complete current state of the equipment in the Projection Service. Private delivery events are not sent to common subscriptions.

## 10. Contract Evolution
| Contract | Versioning | Compatibility rule |
|---|---|---|
| Protobuf schemas in topics | Schema Registry, mode `BACKWARD_TRANSITIVE` for events and `FULL_TRANSITIVE` for the envelope | Only adding optional fields; removing - through `reserved`; changing the meaning of a field - new type |
| Topics | Suffix `.vN` | Incompatible changes - new topic and double-write period |
| REST | Prefix `/v1` | Adding fields and endpoints without changing version; clients ignore unknown fields |
| Connector protocol | Version of the gRPC package (`psim.connector.v1`) and versioning during registration | The gateway supports the current and previous major versions |
| Taxonomy | Version in an event | Only adding types; retirement through `deprecated` |

Compatibility is checked in CI (`buf breaking` and verification in Schema Registry). ADR-004.

## 11. Backpressure and Overload Protection
- Input: Credits in the connector protocol; the gateway gives credits only when the Kafka write queue is below the threshold.
- Kafka consumers: Limited queues between reading and processing; when full - pause partitions (`pause`/`resume`), not unlimited memory growth.
- PostgreSQL: Limited connection pool; when exhausted - waiting with deadlines, then temporary error and pause of the partition.
- REST: Overall user quota within a tenant for all replicas of API Gateway in Valkey (ADR-078), local limits on incoming connections, request size limits, deadlines for each request.
- Mass events: Suppression by rules, notification aggregation (N3), grouping signals into incidents.

## 12. Testability and Module Structure
- For each context: `domain` (pure library) -> `application` (use cases, dependent on ports) -> `adapters` (Kafka, PostgreSQL, Redis, gRPC, HTTP) -> `service` (build and execution).
- Dependency directions are checked by the fitness function: `domain` depends on nothing except the standard library and common domain types.
- Platform ports: `Clock`, `TimerService`, `IdGenerator`, `Random`, `EventPublisher`, `Outbox`, `Inbox`, `StateStore`, `CatalogView`, `ConfigView`, aggregate repositories.
- The simulation environment (ADR-044) substitutes deterministic implementations for ports and emulates Kafka with injected failures.

## 13. Storage in PostgreSQL
- Each service owns its own schema (`catalog`, `incident`, `response`, `command`, `notification`, `audit`, `projection`); access to other schemas is prohibited by rights.
- Migrations - versioned SQL files used by the platform’s migration tool; only expand/contract approach: new fields are added, code is transferred, old ones are removed in the next release. ADR-024.
- Large tables with time (timeline, audit, inbox) are partitioned by time; deleting old data involves detaching partitions.

### 13.1 Storage in ClickHouse

- ClickHouse stores event history, event-signal relationships, incident facts, and signal statistics ([ADR-028](../adr/0028-clickhouse-analytical-storage.md)); it is not a definitive source of truth in the domain but serves as a recoverable storage from Kafka and PostgreSQL for search and analytics. Rebuild is limited by retained source history; recovery beyond that history requires backups.
- Only Event History and Projection Service write in batches, with a deduplication token for insertion; EventHistoryService and ReportService read it.
- Tables are replicated in all topologies; partitioning follows each table schema; event retention uses TTL from `RetentionSettings`; each request is limited to `tenant_id` and time interval.
- Schema migrations are versioned DDLs according to expand/contract rules (ADR-024), implemented using the platform’s migration tools.

## 14. Synchronous API between Web Client and Services

API Gateway acts as a reverse proxy with authentication, authorization, rate limiting, and routing based on the service owner’s path prefix. Services implement their own REST v1 themselves. DTOs are defined in Protobuf; OpenAPI, C++ DTOs and JSON mappings are generated from these contracts as specified in ADR-026.

## 15. Internationalization

The server returns codes and parameters, not localized text (except for `title` errors in English). Localization is handled in the web client (ru, en). Names from catalogs and scenarios are stored as entered by administrators.

## 16. Operational Invariants (ADR-042)

[operational-capabilities.md](operational-capabilities.md) defines device state quality, FileStore, maintenance, responsibility transfer, certificate lifecycle, and replay isolation. Realtime is supplemented by device status, zones, and connectors according to the previous snapshot and delta model; contract changes are executed before implementation and are consistent with client capabilities.

Diagnostic replay uses separate permissions, topics, and storage. Restoring operational projections and reprocessing operational DLQ are independent authorized operations. Responsibility transfer does not break SLA, maintenance does not delete events, and historical backfill does not confirm device state freshness.
