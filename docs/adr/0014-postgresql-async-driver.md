# ADR-014. Asynchronous PostgreSQL driver

<!-- language-links -->
[English](0014-postgresql-async-driver.md) | [Русский](../ru/adr/0014-postgresql-async-driver.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-008, ADR-016

## Context

Services use Boost.Asio coroutines under ADR-016 and must not block event-loop threads on database requests. They need connection pooling, transactions, batch insertion with `COPY`, `LISTEN/NOTIFY` for outbox relays, pipeline mode to reduce latency, and TLS. Integrate the official client with the chosen execution model while retaining control of deadlines, connections and delivery guarantees.

## Considered options

| Option | Advantages | Disadvantages |
|---|---|---|
| libpqxx | Mature, convenient C++ API | Synchronous: requires blocking-call threads; no asynchronous pipeline mode |
| ozo from Yandex | Asynchronous Asio integration | Inactive development and older C++ standards |
| Custom PostgreSQL protocol implementation | Full control | Large scope: SCRAM authentication, TLS, types and `COPY` |
| **Small custom adapter over nonblocking libpq, integrated with Asio** | libpq 18.x is the reference protocol, TLS and SCRAM implementation; nonblocking API and pipeline mode; socket readiness observed through `asio::posix::stream_descriptor` | Custom pooling, coroutine API and result decoding |
| Synchronous libpq in a dedicated thread pool | Simplest approach | Threads per connection; poorer latency and scaling |

## Decision

Use a small `libs/platform/pg` adapter over libpq 18.x for PostgreSQL 18.x: `PQsetnonblocking`, `PQenterPipelineMode`, Asio socket-readiness waits, a coroutine API (`co_await conn.query(...)`), pooling with deadlines, transaction isolation levels, `COPY FROM STDIN` for batch insertion, `LISTEN/NOTIFY` and prepared statements. Type mapping covers a restricted set: int, bigint, text, uuid, timestamptz, jsonb, bytea and arrays.

## Consequences

- The platform library contains the adapter; repository ports isolate domain code from it.
- The fallback is synchronous libpq in the blocking-operation pool behind the same interface.

## Verification

Integration tests against PostgreSQL in Docker cover disconnection, primary failover and deadline cancellation. Benchmarks measure insert throughput and query latency.

## Implementation rules

1. **Dependency:** statically build libpq 18.6 from the official archive, with SHA-256 pinned in `tools/conan/recipes/libpq`. OpenSSL comes from the shared Conan graph. Include the recipe in the lockfile and SBOM; distribution policy permits the PostgreSQL license. Use the local recipe until libpq 18.x is available in Conan Center.
2. **Connection:** resolve DNS asynchronously, then call `PQconnectStartParams`/`PQconnectPoll` with a ready `hostaddr`. Asio observes a duplicate of the libpq-owned socket. The client connects to one endpoint, HAProxy in a cluster, and requires a writable node by default. TLS defaults to `verify-full`; disabling it requires an explicit laboratory setting.
3. **Queries:** use only the parameterized extended query protocol, supporting prepared statements, SQL NULL and binary `bytea`. Results own `PGresult`, expose text values and column OIDs, and validate integer reads. Pass `uuid`, `timestamptz`, `jsonb` and arrays with explicit types or cast them in constant SQL.
4. **Deadlines:** bound socket and connection waits. Timeout, cancellation or transport failure closes the connection; an unfinished request never reaches the next pool borrower. Closing the client does not guarantee immediate server-query cancellation. Server `statement_timeout` and `idle_in_transaction_session_timeout` bound query duration. The pool limits connections, uses FIFO waits, grants exclusive leases and removes connections with unfinished transactions.
5. **Pipeline:** use a bounded batch with one synchronization point. Without explicit BEGIN, the batch forms an implicit transaction. After an SQL error, drain all results through `PGRES_PIPELINE_SYNC` before leaving pipeline mode. Run `COPY` separately outside the pipeline. Follow the [pipeline documentation](https://www.postgresql.org/docs/18/libpq-pipeline-mode.html).
6. **Transactions:** support read committed, repeatable read and serializable with explicit asynchronous COMMIT/ROLLBACK. An unfinished RAII object closes the connection. Check the COMMIT command tag: ROLLBACK returned after an error is not a successful commit. Never automatically retry writes or COMMIT; disconnection can leave their outcome unknown.
7. **Migrations:** use an ordered manifest, separate expand and contract phases, a transaction lock per schema, SHA-256 of applied SQL and history verification. `psim-pg-migrate` runs the selected phase. The release process authorizes contract only after incompatible old versions are removed. Migrations must contain statements allowed inside transactions.
8. **Inbox/outbox:** isolate tables by service schema. Inbox, repository changes and outbox writes share one transaction. Reject an identifier repeated with different content. `in_transaction` implements the unit-of-work pattern; domain cores continue to depend only on ports.
9. **Relay:** allow one transactionally locked batch publication per schema. Kafka acknowledges the whole batch before `published_at` is updated; errors or crashes leave records for retry. PostgreSQL -> Kafka delivery is at-least-once; the consumer inbox ensures one committed effect. Polling is mandatory; `LISTEN/NOTIFY` is an additional signal, so losing a notification does not lose a record.
10. **Errors:** return shared catalog codes and SQLSTATE. SQL text, parameter values, connection strings and PostgreSQL messages are excluded from adapter errors and logs.

Nonblocking sending and reading follow [libpq documentation](https://www.postgresql.org/docs/18/libpq-async.html); connection parameters and DNS requirements follow [connection management](https://www.postgresql.org/docs/18/libpq-connect.html).

## Artifacts and verification

- `libs/platform/pg`: connections, pool, transactions, migrations, unit of work, inbox/outbox and relay. Public interfaces and examples are in the [library README](../../libs/platform/pg/README.md).
- `tools/pg-migrate`: migration CLI. `task bench:pg` measures COPY, pipeline and query p50/p99 in the local environment.
- `task test:component`: real PostgreSQL and Kafka, covering disconnects, deadlines, rollbacks, migrations, publication and retries.
- `task test:pg:sanitizers`: PostgreSQL component scenarios under ASan/UBSan and TSan, including cancellation and SIGKILL.
- Test workers are killed with SIGKILL before COMMIT, after COMMIT and after Kafka acknowledgment. Recovery verifies no committed-event loss and no duplicate inbox effects.
- Full Patroni HA-cluster and node-failover checks belong to cluster-topology qualification. Adapter tests cover disconnection and selection of a writable endpoint.
