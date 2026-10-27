# ClickHouse Layer

<!-- language-links -->
[English](README.md) | [Русский](../../../docs/ru/repository/libs/platform/clickhouse/README.md)
<!-- /language-links -->

[ADR-028](../../../docs/adr/0028-clickhouse-analytical-storage.md) · [ADR-045](../../../docs/adr/0045-clickhouse-layer.md) · `psim::platform::clickhouse`

The native client `clickhouse-cpp/2.6.1` uses TLS, an asynchronous pool, request building, a Kafka loader template, and expand/contract migrations. Event history and business projections are implemented on this layer by Event History and Projection Service.

## Client and Requests

`Pool` is associated with one executor/shard. It limits the number of connections through `Semaphore`, and the waiting time for a free connection is controlled by `acquire_timeout`. The synchronous client is executed only in `BlockingPool`. Settings are determined by the value `PoolOptions`. All dependencies must remain active until the operations are completed.

By default, TLS with CA verification and DNS name, SNI, the common profile `security::TlsOptions`, the client certificate, and CRL are enabled, if available. The password is read through `read_secret` from the file of the process owner with rights `0400`. `allow_plaintext` is a special exception for isolated development. Errors do not contain SQL, parameters, passwords, or server response text.

`select(Select)` requires a table, an explicit column list, `tenant`, a half-open time interval `[from_ms, to_ms)`, and a positive row limit. Identifiers are checked separately; values including binary strings and filters `equals` are passed as native parameters. Parameters tenant/time are reserved within the builder and cannot be overridden by filters. `FINAL` is included by default. `max_execution_time`, `max_result_rows`, `max_result_bytes`, and the client-side validation prevent returning a partial response as a successful result.

`execute_ddl` only accepts trusted configuration and migration constants. This is an administrative interface; user requests through it are not processed. In the delivery, the loader only has SELECT/INSERT operations on its tables, while the reader only has SELECT operations. The migrator has a separate account.

After an error occurs, the connection is destroyed; a new session selects the next replica. An unknown result of an insertion does not trigger a hidden repetition. A native call ends under the constraints of the socket and server before returning the connection to the pool; canceling Asio cannot immediately stop the synchronous driver. The socket timeout limits individual input/output operations, not the total time for any DDL. `close()` wakes up pending operations and prevents new ones from occurring; active calls end with their own session.

## Loader

`Loader` receives `Pool`, `pg::Pool`, `kafka::Consumer`, a pre-initialized transactional `kafka::Producer`, `LoaderOptions`, and `Converter`. Call `run_once()` sequentially; if an error occurs, apply a limited backoff strategy or terminate the service. The producer must have a stable unique transactional id with fencing from the previous instance. Recreating an instance does not change the consumer group, loader name, or destination.

`Converter` receives Kafka messages from one partition and returns a column-based `Converted`: one row for each successful message, while indices of invalid records are returned in `rejected`. Protobuf is parsed using the existing `ProtobufSerde<T>`; fields are added directly to the typed columns, without using intermediate maps or JSON. An example of the conversion is shown in the [component fixture](tests/support.hpp). Invalid indices, missing records, and errors other than `PROCESSING_PAYLOAD_INVALID` will stop the loading process.

Sequence:

1. Build the batch to `max_messages`, `max_bytes`, or `flush_after`, and group them by partitions. The next batch will not be read during insertion: backpressure limits memory.
2. Save the native columns, DLQ, and offset boundaries in `psim_clickhouse.pending_batches` and PostgreSQL. This is a log of only unfinished batchs. Its schema is created with the `Loader::initialize()` installation account.
3. Insert the batch into ClickHouse with a token derived from SHA-256 of the destination, topic, partition, offset range, and content. Record the confirmation of insertion in PostgreSQL.
4. When there are erroneous records in a Kafka transaction, publish the DLQ and record the input offsets. A batch without DLQ only records the offsets using a regular commit after a durable confirmation of insertion. Remove the completed log entry.

After restarting, the log of assigned partitions is processed first, with the same bytes and token. Changing the batch size or triggering a timer does not change an unfinished batch. In case of failure in the same process, reading reverts to safe offsets through `seek_and_wait` to BlockingPool. Already registered Kafka offsets prevent repeated DLQ after a crash before clearing the log. Incorrect records retain the original bytes and headers along with the error code and source coordinates.

The deduplication of ClickHouse has a finite window in terms of the number of blocks and time. Settings `replicated_deduplication_window` and `replicated_deduplication_window_seconds` should cover all loaders’ capacities and the maximum recovery time. Within the window, repeated exact batchs do not add physical rows. After the window expires, ADR-028 protection applies - `ReplicatedReplacingMergeTree` with a stable write and read key through `FINAL`. Until the parts are merged, physical repetitions are possible. This is not an unlimited distributed transaction between Kafka, PostgreSQL, and ClickHouse. In case of physical loss of confirmed data, tables are recovered from sources, as specified by ADR-028.

When changing the transformation, first drain the previous log. The name of the loader is versioned when its semantics change. The table must ensure stable keys, versioning, and partitioning for reproducibility; the use of wall-clock time and randomness in the transformer is prohibited.

## Migrations and Metrics

`Migrator` stores the SHA-256 fingerprint of the plan and the number of executed DDL operations in PostgreSQL. The Advisory lock serializes the execution for the target cluster/database; steps are recorded separately. After a failure, the last DDL operation may be reexecuted, so each operation must be idempotent. History with changed checksums, skipped or newer versions is rejected. `contract` is initiated as a separate phase after draining old services and completing the corresponding `expand`. SQL operations with `ON CLUSTER` use the same Replicated-DDL and macros across all topologies.

`Metrics` records the duration of requests and inserts, batch size, confirmed rows, errors, DLQ, and the age of the last event in Kafka. Names start with `psim_clickhouse_`; identifiers for tenants, SQL statements, and values do not become tags.

## Verifications

```bash
task env:up
task test:clickhouse
task test:clickhouse:replica
task test:clickhouse:sanitizers
task test:component
task bench:clickhouse
```


The `clickhouse-ha` profile adds two isolated replicas with the native mTLS. These replicas share a common Keeper and least-privilege accounts. They store data in separate, anonymous volumes, which are replaced during component runs. Stopping and starting one replica preserves its data. The main ClickHouse from the local environment is stored separately. Component checks are executed in order: network failures must not affect parallel tests.

Binary parameters are checked, tenant isolation is verified, TLS trust is confirmed, roles and restrictions are evaluated, reinsertion is tested, connection loss and replica switching are monitored, the actual process of replica termination are checked, and verification occurs after startup (`test:clickhouse:replica`). Migration, direct conversion of Protobuf to columns, and transaction DLQ are also tested. SIGKILL is used after preparation, insertion, acknowledgment in the DB, and Kafka storage to verify recovery with a different batch size, number of physical rows, uniqueness of identifiers, and unique DLQ entries. All four boundaries are tested for batchs with and without rejected records.

The `psim_clickhouse_throughput` benchmark measures column preparation and synchronous native insertion with TLS, then checks the number of rows. The threshold is 100,000 rows per second; this is the throughput of the adapter. Event History throughput is measured by its service benchmark and full system load qualification. The module coverage threshold is 75% across all module and component scenarios.
