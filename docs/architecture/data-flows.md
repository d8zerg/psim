# Data Flows and Delivery Semantics

<!-- language-links -->
[English](data-flows.md) | [Русский](../ru/architecture/data-flows.md)
<!-- /language-links -->

For each flow, its producer, consumers, key, order, semantics, idempotence mechanism, and behavior in case of failure are defined. Semantic decisions are specified in ADR-006, while partitioning decisions are outlined in ADR-007. The number of partitions and scaling are described in [scaling.md](scaling.md).

---

## 1. General Kafka Client Settings

| Parameter | Value | Reason |
|---|---|---|
| Producer `acks` | `all` | The record is confirmed by all in-sync replicas |
| Producer `enable.idempotence` | `true` | No duplicates due to retries within the session |
| Producer `compression.type` | `zstd` | Storage and network capacity ([scaling.md](scaling.md#5-capacity-planning)) |
| Producer `linger.ms` | 5 by default | Latency and batch size tradeoff |
| Topic `replication.factor` / `min.insync.replicas` | 3 / 2 in cluster; 1 / 1 on a single machine | RPO = 0 in cluster if one broker fails |
| `unclean.leader.election.enable` | `false` | Do not lose confirmed records |
| Consumer `enable.auto.commit` | `false` | Offset is only recorded after processing |
| Consumer `isolation.level` | `read_committed` | Do not see unfinished transactions |
| Consumer `group.protocol` | `consumer` (KIP-848) | Incremental assignment; classic fallback uses `partition.assignment.strategy=cooperative-sticky` |
| Consumer `group.instance.id` | Not set | Dynamic membership permits restart without waiting for a static member identity |
| Broker `group.consumer.session.timeout.ms` | 45,000 by default; 10,000 in the local profile | The deployed timeout must be included in RTO qualification |

## 2. Semantic Classes

| Class | Mechanism | Where Used |
|---|---|---|
| **EOS-K** - exactly-once within Kafka | "Read - Process - Write" transaction: output messages, changelog changes, and input offsets are atomically recorded | Normalizer, Correlation Engine |
| **EOS-DB** - effectively exactly-once at the PostgreSQL boundary | Inbox: records the identifier of the input message in the same database transaction as the aggregate change; offsets are recorded after a database commit; retries are discarded based on the inbox | Incident Service, Response Engine, Command Service, Audit Service |
| **Outbox** | The event is recorded in the outbox during an aggregate change transaction; the relay publishes it to Kafka and marks it as sent. Republication is possible after a relay failure; consumers are idempotent | All services with PostgreSQL |
| **ALO+ID** - at-least-once with idempotence by identifier | Repetitions are possible; consumers discard them based on the identifier or aggregate version | Input, commands, audit, projections |

Normalizer uses EOS-K. Duplicates in `psim.events.normalized.v1` distort the correlation threshold rules, and removing them at lower levels of the flow would require a deduplication horizon of minutes at 50,000 events per second. Transactions move deduplication to a single location with compact state (a tracker for source sequence numbers). Changing delivery semantics requires a separate architecture decision and renewed correctness and latency qualification. The solution is specified in ADR-006.

---

## 3. Flows

### 3.1 Input: Connector -> `psim.ingest.raw.v1`

| Property | Value |
|---|---|
| Producer | Connector Gateway |
| Consumers | Normalizer (group `psim-normalizer`) |
| Key | `source_id` |
| Order | Within the source - in the order of SDK sending |
| Semantics | ALO+ID: the connector receives batch confirmation only after `acks=all` (CS2); unconfirmed SDK re-sends from disk buffer |
| Idempotence | `event_id` and `(source_id, source_epoch, source_seq)`; duplicates are removed by Normalizer |
| Retention | 6 hours by default; minimum - the time for Normalizer to recover with a backup |

**Behavior in case of failure:**

| Failure | Behavior |
|---|---|
| Gateway down to recording in Kafka | The batch is not confirmed; SDK reconnects to another instance and re-sends |
| Gateway down after recording, before confirmation | SDK re-sends; a duplicate appears in the topic; Normalizer discards it based on the tracker |
| Kafka unavailable or slow | The gateway stops issuing credits; the SDK buffers on disk. When full, it returns backpressure to the source without evicting stored events |
| Invalid message | Discarded by the gateway with an error code; does not reach Kafka; recorded in the connector metrics |

### 3.2 Normalization: `psim.ingest.raw.v1` -> `psim.events.normalized.v1`

| Property | Value |
|---|---|
| Producer | Normalizer |
| Consumers | Correlation Engine (`psim-correlation`), Event History (`psim-event-history`), Projection Service (console models; only live significant observations) |
| Key | `site_id + ":" + zone_id` |
| Order | Within the zone; the source order is preserved because the source belongs to the same zone |
| Semantics | EOS-K |
| State | Source tracker: for `(source_id, source_epoch)` - the maximum number and bitmap of the last 4096 numbers. Stored in memory and in the compacted topic `psim.normalizer.state.v1` in the same transaction |
| Idempotence | Repetitions with known numbers are discarded; a gap in numbers generates `system.source.sequence_gap` |
| Retention | 24 hours by default; adjustable ([scaling.md](scaling.md#52-estimation-at-50000-eventss)) |

**Changing the device zone.** When `DeviceMoved`, events start to be assigned to a new zone. The order between events before and after the transfer is not guaranteed; correlation rules work based on event time, and this is allowed.

**Behavior in case of failure:**

| Failure | Behavior |
|---|---|
| Instance down | The transaction is interrupted; the new partition owner recovers the tracker from `psim.normalizer.state.v1` and continues from the recorded offset; no duplicates |
| No device or mapping rules | The message goes to `psim.dlq.normalizer.v1` in the same transaction |
| Outdated catalog replica | Normalization continues based on the current copy; the catalog replica catches up within ≤ 2 seconds (S2 requirement) |

### 3.3 Correlation: `psim.events.normalized.v1` -> `psim.signals.v1`

| Property | Value |
|---|---|
| Producer | Correlation Engine |
| Consumers | Incident Service (`psim-incident`), Event History (`psim-event-history`, event relationships with signals), Projection Service |
| Key | `grouping_key` signal |
| Order | Within the key grouping |
| Semantics | EOS-K: output signals, changelog records, and input offsets - in one transaction |
| Idempotence | Deterministic `signal_id` (SG1); protection on the Incident Service side - inbox |
| Retention | 7 days |

The key for the output topic is `grouping_key`: The Incident Service receives all signals of a future incident in one partition in order. This serializes normal consumption for a key; database invariants also protect incident creation against redelivery and rebalance races. This is documented in ADR-007.

**Internal correlation topics:**

| Topic | Key | Purpose |
|---|---|---|
| `psim.correlation.changelog.v1` | `tenant_id/rule_id/rule_version/correlation_key` | Compacted; restoring RocksDB. The partitions match the topic of redistribution one-to-one. |
| `psim.correlation.repartition.v1` | `correlation_key` | Events from all scopes with unchanged rule versions. The router records output and input offsets in one transaction; the processor reads based on the correlation key. |

**Time.** Rules operate based on event time. The partition watermark is the minimum from the sources of the partition `max(occurred_at) - allowed_lateness`, except for inactive sources during downtime. Rules `conjunction`, `sequence`, and `single` are triggered when the final event arrives, without waiting for the watermark. Rules `threshold` and `absence` close the window based on the watermark. Events that exceed the allowable delay are not counted in closed windows but are counted in the metrics. Details - ADR-010.

**Behavior in case of failures:**

| Failure | Behavior |
|---|---|
| Instance failure | The transaction is interrupted; the new owner restores RocksDB from the changelog (≤ 30 seconds), and continues with the recorded offset. |
| Rebalancing | Cooperative: Only reassignments are revoked; the transaction is recorded before revocation. |
| Change of rule set | Applied at the edge of the offset `psim.config.v1` (RS2); the state of unchanged rules is preserved (RS3). |
| Error in calculating the rule for an event | The event and error are in `psim.dlq.correlation.v1`; other rules process the event. |

### 3.4 Incidents: `psim.signals.v1` -> PostgreSQL -> `psim.incidents.events.v1`

| Property | Value |
|---|---|
| Producers | Incident Service (outbox) |
| Consumers | Response Engine (`psim-response`), Projection Service, API Gateway realtime (reading without groups, all partitions), Audit Service (through `psim.audit.v1`) |
| Key | `incident_id` |
| Order | Within the incident; events carry `version` of the aggregate without skipping. |
| Semantics | Input to EOS-DB (inbox by `signal_id`), output Outbox |
| Idempotency | Consumers apply the event only if `version` is greater than the applied value; repetition with the same version is ignored. |
| Retention | 30 days (source for rebuilding projections and reports) |

Publication order of outbox: The relay publishes records in the order `outbox_id` within the aggregate; when there are multiple relays, the outbox partitions are distributed according to `hash(incident_id)`, so that one aggregate publishes only one instance.

**Behavior in case of failures:**

| Failure | Behavior |
|---|---|
| Failure after database commit, before offset recording | The signal is sent repeatedly; the inbox discards it. |
| Relay failure after publication, before marking | The event is published again with the same version; consumers ignore it. |
| PostgreSQL unavailable | Consumption of signals is suspended (backpressure); signals are stored in Kafka; after recovery, re-processing occurs. |
| PostgreSQL switches to a replica | Synchronous replication: recorded transactions are not lost (RPO = 0). |

### 3.5 Response: `psim.response.events.v1`

| Property | Value |
|---|---|
| Producers | Response Engine, Command Service, Notification Service (outbox) |
| Consumers | Incident Service (`blocking_steps`, timeline), Response Engine (results of commands), Command Service (requests from scenarios), Projection Service, Notification Service, API Gateway realtime (all partitions, only allowed variants) |
| Key | `incident_id`; for duty - `site_id`; for commands and notifications outside the incident - `command_id` or `notification_id` |
| Order | Within the incident |
| Semantics | Outbox; consumers - EOS-DB or by version of the aggregate |
| Retention | 30 days |

The topic publishes events of scenario execution, command lifecycle, and notifications, as well as durable CommandRequested and NotificationRequested. Separate topics for command lifecycle are not created: consoles and projections need a single stream per incident.

### 3.6 Commands: `psim.commands.v1` and `psim.commands.results.v1`

| Property | `psim.commands.v1` | `psim.commands.results.v1` |
|---|---|---|
| Producers | Command Service (outbox) | Connector Gateway |
| Consumers | All instances of Connector Gateway: each reads all partitions and delivers the command if the session of the connector has it | Command Service (`psim-command`) |
| Key | `connector_id` | `command_id` |
| Order | Within the connector | Within the command |
| Semantics | ALO+ID: SDK executes `command_id` no more than once and returns the saved result | ALO+ID: the result for the final command is ignored (CM5) |
| Retention | 24 hours | 24 hours |

Broadcast reading is chosen because the amount of commands is small (units per second), and routing through the session registry adds dependency on Redis at a critical point. Before sending, Command Service checks the session registry: if the connector is not online, the command is immediately terminated with `failed: COMMAND_CONNECTOR_OFFLINE`. If the session disappears between checking and delivery, no instance will deliver the command, and it will terminate with `deadline_at` with status `timed_out`. The session registry is used here only for quick rejection, not for delivery. The solution is documented in ADR-018.

### 3.7 Catalog: `psim.catalog.v1`

| Property | Value |
|---|---|
| Producers | Resource Catalog (resources) and Connector Gateway (registrations); outbox of each owner |
| Consumers | Normalizer, Connector Gateway, Correlation Engine, Response Engine, Command Service, Projection Service (access scope snapshots) |
| Key | `resource_type + ":" + resource_id` |
| Semantics | Compacted; the record is the complete current state of the resource with version |
| Reading | Each instance reads the topic completely without consumers groups, builds a local model, and is considered ready after reading to the end; Projection Service stores the overall model in PostgreSQL |
| Retention | Indefinitely (compaction) |

### 3.8 Configuration: `psim.config.v1`

| Property | Value |
|---|---|
| Producers | Correlation Engine (rule sets), Resource Catalog (mappings, retention, processing settings, maintenance) |
| Key | `config_key`: `ruleset/<tenant>`, `mapping/<tenant>/<connector_type>`, `retention/<tenant>`, `maintenance/<tenant>/<id>`, `system/<name>` |
| Partitions | 1 - the order in which all configuration changes are applied |
| Semantics | Compacted; record - a complete active object with a version |
| Application | The consumer applies changes atomically between input batchs |

### 3.9 Audit: `psim.audit.v1`

| Property | Value |
|---|---|
| Producers | All services (outbox or in Kafka transactions for Normalizer and Correlation Engine) |
| Consumers | Audit Service (`psim-audit`) |
| Key | `tenant_id` |
| Order | Within a tenant - the order of recording in the chain |
| Semantics | ALO + idempotence according to `source_event_id` (AU3) |
| Retention | 30 days (source of truth after recording - PostgreSQL Audit Service) |

### 3.10 Errors: `psim.dlq.<stage>.v1`

| Property | Value |
|---|---|
| Producers | Stages of the pipeline: `normalizer`, `correlation`, `incident`, `response`, `command`, `audit`, `projection` |
| Key | Original message key |
| Content | Original bytes without changes; headers: `psim-dlq-stage`, `psim-dlq-error-code`, `psim-dlq-error`, `psim-dlq-source-topic`, `psim-dlq-source-partition`, `psim-dlq-source-offset`, `psim-dlq-at` |
| Processing | Administrative CLI: viewing, re-processing in the original topic, discarding with audit recording |
| Retention | 14 days |

Rule: Only messages that cannot be processed without intervention (invalid data, missing reference materials) are sent to DLQ. Temporary errors (storage availability issues) lead to pauses and retries, not DLQ - otherwise the order will be disrupted.

---

## 4. Summary Table

<!-- kafka-topic-summary -->

| Topic | Key | Semantics | Default Retention | Reason |
|---|---|---|---|---|
| `psim.ingest.raw.v1` | `source_id` | ALO+ID | 6 hours | - |
| `psim.ingest.state.v1` | `connector_id` | ALO+ID | 7 days | - |
| `psim.events.normalized.v1` | `site_id:zone_id` | EOS-K | 24 hours | EOS-K instead of at-least-once |
| `psim.normalizer.state.v1` | `tenant_id/source_id:epoch` | compacted | ∞ | New internal topic |
| `psim.signals.v1` | `grouping_key` | EOS-K | 7 days | Key `grouping_key` instead of `correlation_key` |
| `psim.correlation.changelog.v1` | `tenant_id/rule_id/rule_version/correlation_key` | compacted | ∞ | - |
| `psim.correlation.repartition.v1` | `correlation_key` | EOS-K | 6 hours | New internal topic |
| `psim.incidents.events.v1` | `incident_id` | Outbox + version | 30 days | - |
| `psim.response.events.v1` | `incident_id or site_id` | Outbox + version | 30 days | Includes command and notification lifecycle |
| `psim.commands.v1` | `connector_id` | ALO+ID | 24 hours | - |
| `psim.commands.results.v1` | `command_id` | ALO+ID | 24 hours | - |
| `psim.catalog.v1` | `resource_type:resource_id` | compacted | ∞ | - |
| `psim.config.v1` | `config_key` | compacted, 1 partition | ∞ | - |
| `psim.audit.v1` | `tenant_id` | ALO+ID | 30 days | - |
| `psim.dlq.<stage>.v1` | Original | ALO | 14 days | - |

All retention times are configured during installation and through administration. Semantics of delivery and partitioning are defined in ADR-006 and ADR-007.
