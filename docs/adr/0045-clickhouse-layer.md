# ADR-045. Native ClickHouse layer and Kafka batch recovery

<!-- language-links -->
[English](0045-clickhouse-layer.md) | [Русский](../ru/adr/0045-clickhouse-layer.md)
<!-- /language-links -->

- **Status:** Accepted
- **Supplements:** [ADR-028](0028-clickhouse-analytical-storage.md)
- **Related to:** ADR-014, ADR-016, ADR-024, ADR-041, ADR-043

## Context

The token of the topic, partition, and range of offsets does not ensure batch repetition if the timer or batch size after a failure creates other boundaries. DLQ must also align with offset recording. The native client is synchronous; its socket cannot be passed to another request until the current call is completed. DDL ClickHouse does not participate in PostgreSQL transactions.

## Solution

1. Register `clickhouse-cpp/2.6.1`, Apache-2.0, in Conan with OpenSSL. Perform native operations in the existing `BlockingPool`; the connection pool is bounded per shard. After an error, remove the connection and select the next replica; writes with unknown outcomes should not be automatically repeated.
2. Use a common TLS security profile with CA, name verification, SNI, client certificate, and CRL if available. Secrets should be read from secure files. The loader, reader, and migrator have separate rights; SQL and server diagnostic responses do not enter platform errors.
3. `Select` constructs SQL only from verified identifiers. All values are native parameters. `tenant_id`, time interval, and limits on rows, bytes, and duration are required. Trusted DDL is separated into administrative interface.
4. Before insertion, save prepared columns, original DLQ records, assignments, and offset boundaries in PostgreSQL through the platform library. Only incomplete batchs should be saved. Repetition uses original bytes and SHA-256 token; the transformation name is stable and versioned when semantics change.
5. After confirmation of insertion, mark the batch in the log; publish DLQ with one Kafka transaction and record input offsets. Before cleaning up a recovered journal, check committed offsets. Group generation and transactional id fence previous instances; a journal row lock serializes batch insertion.
6. Save limited deduplication semantics from ADR-028. Configure ClickHouse window based on total blocks and recovery time. For repetitions outside the window, use stable keys `ReplicatedReplacingMergeTree` and `FINAL`; physical repetitions before merging are allowed outside the window. Physical loss of confirmed data requires restoration from sources.
7. Keep checksums and progress of DDL in PostgreSQL, serialize statements with an advisory lock. Save plans before the first DDL. Each operation is idempotent: after a failure, the last one can repeat. `contract` is a separate run after `expand` is completed and old versions are drained.
8. Check the adapter against two replicas of ClickHouse with Keeper, mTLS, limited roles, and network failures. The actual SIGKILL at delivery boundaries should restore the batch if the read size changes. The simulator remains manual and does not replace component checks.

## Consequences

- ClickHouse adapter depends on PostgreSQL layer. A service schema, log rights, and its backup are needed; domain history is not stored there.
- Uncompleted batchs are limited by size settings; reading is paused during delivery. This limits memory and spreads backpressure to Kafka.
- Synchronous driver calls are drained until the connection is freed. Server limits and socket timeouts operate at different levels; Asio cancellation does not interrupt system calls immediately.
- Schemas for domain history, retention, and reports are defined in ADR-052, ADR-054, and ADR-057. The library provides infrastructure mechanisms; the domain model belongs to services.

## Verification

`task test:clickhouse`, `task test:clickhouse:sanitizers`, `task test:component`, `task bench:clickhouse`. Insertion threshold is 100,000 rows/s, adapter coverage is 75%. Requests are checked for binary values and SQL injection attempts; tenant/time cannot be excluded from the builder.

## Sources

- [Native ClickHouse client](https://github.com/ClickHouse/clickhouse-cpp/tree/v2.6.1).
- [Deduplication of repeated insertions](https://clickhouse.com/docs/guides/developer/deduplicating-inserts-on-retries).
