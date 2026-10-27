# Normalizer and Event History

<!-- language-links -->
[English](normalization-and-history.md) | [Русский](../ru/engineering/normalization-and-history.md)
<!-- /language-links -->

`psim-normalizer` accepts `psim.ingest.raw.v1`, applies a taxonomy and its local catalog replica, publishes `psim.events.normalized.v1` and the transactional cursor changelog. `psim-event-history` loads the normalized stream into ClickHouse. Recovery boundaries and constraints are defined in [ADR-052](../adr/0052-normalization-and-event-history.md).

## Local Verification

```sh
task processing:test
task processing:sanitizers
task processing:coverage
task processing:bench
```

All processes are executed through the native Docker Engine on Linux. The verification creates a Compose project `psim-processing-test`; `PROJECT` sets another name. The custom project is deleted along with test volumes. If the network already exists, the verification only stops its services. Logs and reports are recorded in `build/processing/<preset>/`.

Examples of configurations: [Normalizer](../../deploy/local/config/event-flow/normalizer.yaml), [Event History](../../deploy/local/config/processing/event-history.yaml). ClickHouse password is a protected 0400 mode file belonging to the process user. For real delivery, TLS/mTLS is used for Kafka, Schema Registry, PostgreSQL, and ClickHouse. Plaintext connections are only allowed with explicit configuration in the local environment.

## Management of Normalization

The catalog comes from `psim.catalog.v1`. `ConfigRecord.mapping_rule_set` is published in `psim.config.v1` under the key `mapping/<tenant_id>/<connector_type>`. The set includes the version, taxonomy version `1`, and ordered rules. Example rule:

```json
{
  "rawCode": "ALARM*",
  "condition": "has(raw.attributes[\"loop\"]) && raw.attributes[\"loop\"] == \"1\"",
  "type": "intrusion.zone.alarm",
  "attributeMap": {"loop": "loop"}
}
```

The first match determines the type, class, and severity. An empty condition is always true. The supported subset of CEL is described in ADR-052; an unsupported expression stops the loading of a corrupted copy, and the previous correct configuration is not replaced. Resource Catalog publishes versions through `PUT /v1/mappings/{connectorType}` via API Gateway, with `If-Match` version checks. The [administration interface](administration.md) provides the mapping editor; concurrent publication by a separate administrative writer is not allowed.

`ProcessingSettings` sets the significance threshold. `taxonomy_file` specifies the readable YAML: the package installs it in `/usr/share/psim/taxonomy.yaml`, and the local test environment reads the repository file. The number of resources and cursors is limited by `max_replica_entries`. Cursors are not automatically deleted. Old numbers outside the range 4096 are sent to DLQ with a reason. The read batch has a limit of 16 messages, with a target confirmation interval of 5 ms and a minimum interval between transactions of 5 ms (`transaction_gap_ms`). Cursors from the same source are coalesced into the latest snapshot within a transaction.

## Status and History

The presence of an event in history does not prove device state freshness. Only recently confirmed observations by the current receiving session receive `live_observation` and `state_observed_at`; other events retain their time without increasing freshness. When reading, the projection takes into account the current Gateway session and the monotonicity of confirmation time.

Event History uses table `events`, log `psim_clickhouse.pending_batches`, and a separate transaction error publication. The prepared block is limited to 8 MiB, including serialization. If the conversion exceeds the limit, the loader reduces the prefix of the records and rereads the remaining part after recording. The default response limit for PostgreSQL is 32 MiB: reading the binary log in hex requires double the size. A failure in ClickHouse leaves the prepared block for repetition. Transient errors are not transferred to DLQ. Invalid normalized Protobuf is sent to DLQ with the original payload. Reading history uses `FINAL`, a required tenant, and a limited time interval.

The expiration time is stored in each row. `RetentionSettings` changes the expiration times of newly prepared blocks; previously saved rows retain their original expiration time. After a failure, repetition uses the original bytes of the block. TTL is executed through ClickHouse background merging and is not an exact timer for deletion.

## Delivery

Both services have separate Debian packages and container images running as a non-root user. Normalizer requires Kafka, Schema Registry, and taxonomy file. Event History additionally requires PostgreSQL for logs, ClickHouse, and Keeper. Migration creates one Replicated table on the configured cluster; to run, DDL rights and log creation are required. The runtime role should only receive necessary rights after migration.

## Stage Measurement

`p99_stage_ms` is measured from the start of reading the next input batch of Normalizer until the consumer receives the normalized record `read_committed`. Reading, batch processing, recording, and transaction visibility are included. A separate `p99_visible_ms` measures the time from receiving by the gateway and includes the delivery of raw events through Kafka. For Normalizer budget, 30 ms is used for the first value; the total delay is reported. Load testing uses an open-loop schedule of 50,000 events per second, a separate warm-up of 50,000 events, 100 sources in 100 zones, and full comparison of identifiers. Comparing a saturated flow on one and three instances requires at least a 2.4-fold increase in speed. The generator uses a separate synthetic code `BENCHMARK`, mapped to `generic.alarm`, to prevent incidents from the baseline test environment.

Verified technical profile: 100,000 measured events after 50,000 warm-up, three Normalizers, and two Event History on one Linux host with one Kafka. p99 of the stage is 27.20 ms, the upper estimate of history delay is 4.32 s, and the increase in saturated flow on three instances is 3.92 times. The main measurement uses exact-code mapping without CEL conditions and events without additional attributes; large attributes are tested with a separate functional scenario. This is a short stage measurement; long-term load and cluster SLOs are tested separately.
