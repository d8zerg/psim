# PostgreSQL Layer

<!-- language-links -->
[English](README.md) | [Русский](../../../docs/ru/repository/libs/platform/pg/README.md)
<!-- /language-links -->

[ADR-014](../../../docs/adr/0014-postgresql-async-driver.md) · PostgreSQL and libpq 18.x · target `psim::platform_pg`.

The library integrates libpq with Boost.Asio. Connection, pool, lease, and transactions are used in one shard; one request runs per connection. libpq and its results are hidden behind public interfaces.

libpq 18.6 is built using the [local Conan recipe](../../../tools/conan/recipes/libpq/conanfile.py) from the official repository, with SHA-256 verification. Versions and revisions are defined in `conan.lock`; the build tasks automatically export the recipe. The package includes a static client with OpenSSL, but the server PostgreSQL is not built using this recipe.

The build also exports the [librdkafka 2.14.2 recipe](../../../tools/conan/recipes/librdkafka/conanfile.py) with atomic consumer-group flags: inter-component checks under TSan identified a race between heartbeat and queue reading. The patch applies to all profiles; details are in [ADR-041](../../../docs/adr/0041-kafka-layer.md).

## Connection and Requests

`Connection::connect` resolves names through Asio and connects to one endpoint. For the cluster of endpoints, HAProxy is used; default values are `target_session_attrs=read-write` and `sslmode=verify-full`. Parameters for CA, client certificate, and key are sent via `ConnectionOptions::properties`. Password is sent as a separate field and is not included in errors.

```cpp
using namespace psim::platform;

pg::Pool pool({.connection = {.host = "database", .properties = {{"sslrootcert", "/etc/psim/ca.pem"}}},
               .size = 8});
auto lease = co_await pool.acquire();
if (!lease) {
  co_return std::unexpected(lease.error());
}
auto rows = co_await (*lease)->query({.sql = "SELECT version FROM incident WHERE id = $1::uuid",
                                    .parameters = {pg::Parameter::text(incident_id)}});
```

SQL is limited to 1 MiB; the size of one request and the entire pipeline including SQL is 16 MiB. SQL remains constant; data is sent via `$1`, `$2`, and other parameters. `Parameter` stores values, distinguishes NULL from empty strings, and supports binary data. The result of `Rows` occupies memory; `value` returns a view, `integer` checks conversion, metadata is available via `column_name` and `column_type`. Other types are read as text and converted by the repository adapter.

Prepared statements are created via `prepare` and executed via `execute`. They belong to a specific connection: they need to be prepared again after being replaced.

## Deadlines and Pool

Connection, lease issuance, and requests are limited by deadlines. The pool creates connections as needed, limits their number, and serves pending requests in order of arrival. Lease returns only connections without active transactions or unfinished protocol exchanges. `close` wakes up pending connections and closes free ones; active leases remain valid until returned.

Timeouts, cancellations, and transport breaks close connections. A sent request can still be executed on the server; its duration is limited by server timeouts. An error returned for a write or COMMIT does not mean the operation failed. The library does not automatically repeat it; business operations rely on the inbox, idempotence, and state synchronization.

`query_timeout` sets client limits, `properties.options` sets server parameters. Default values are `statement_timeout` and `idle_in_transaction_session_timeout`, which are 30 seconds. The pool, connection, and related objects must remain alive until the running routines are completed.

## Pipeline, COPY, and Notifications

- `pipeline` sends up to 1024 parameterized requests before reading responses. Without an explicit transaction, the batch is atomic. An SQL error aborts the implicit transaction; results are drained until synchronization point, then the connection can be used again. The memory limit of the result applies to the entire batch. It is checked after receiving libpq results; SQL should limit the sample size itself.
- `copy_in` accepts constant SQL `COPY ... FROM STDIN` and prepared text or binary data. COPY is performed outside the pipeline.
- `notification` waits for an event after `LISTEN` on a dedicated connection. Notifications help wake up consumers faster; long-term state checks are performed independently of them.

## Transactions, Inbox, and Outbox

`Transaction::begin` supports read committed, repeatable read, and serializable. COMMIT and ROLLBACK are explicitly called; destroying an unfinished transaction closes its connection. Receiving the ROLLBACK tag instead of COMMIT returns an error.

`in_transaction<T>` is a template for infrastructure repositories. Message processing includes inbox, domain changes, and outbox in one transaction:

```cpp
pg::MessageStore store("incident_service");
auto result = co_await pg::in_transaction<void>(&pool,
    [&](pg::Transaction* transaction) -> boost::asio::awaitable<Result<void>> {
      auto accepted = co_await store.accept(transaction, "incident-handler", event_id, content_hash);
      if (!accepted) {
        co_return std::unexpected(accepted.error());
      }
      if (!*accepted) {
        co_return Result<void>{};
      }
      auto changed = co_await transaction->connection().query({
          .sql = "UPDATE incident SET version = version + 1 WHERE id = $1::uuid",
          .parameters = {pg::Parameter::text(incident_id)}});
      if (!changed) {
        co_return std::unexpected(changed.error());
      }
      co_return co_await store.append(transaction, outgoing);
    });
// Commit the input Kafka offset only after result succeeds.
```

`MessageStore::initialize` creates schemas and tables before running processors. This operation is performed by the database preparation phase with DDL rights. Repeating inbox/outbox with different content is rejected. The inbox fingerprint is sent to the processor; it must reflect the unchangeable content of the event.

The payload outbox is limited to 1 MiB, key to 64 KiB, JSON with hex-encoded headers to 64 KiB, and identifier to 1024 bytes. Binary values of headers and repeated names are saved.

The limits of `message.max.bytes` for the producer, `max.message.bytes` for the topic/broker, and replication/read size must cover the entire message with key and headers. These settings are determined by service configuration and environment; the library does not change them itself.

`OutboxRelay` limits the batch by number of rows and 4 MiB of encoded data (the connection result limit should be at least 8 MiB). Large messages are published in smaller batchs. The relay reads pending rows and holds a transaction advisory lock on the schema. `kafka_publisher` uses a dedicated idempotent `Producer` with empty `transactional_id`, sends the batch, and waits for acknowledgments from Kafka. An open Kafka transaction is rejected: delivery acknowledgment does not make the message available to consumers `read_committed`. Only then does the relay mark the rows as published and commit the transaction. The identifier is saved in the header `psim-event-id`; key, binary payload, headers, and explicit partition are saved.

If an error occurs after acknowledgment, the batch is republished. Delivery has a semantic of at least once; the consumer’s inbox ensures one recorded effect. Multiple relays on the same schema select the only active publisher. `run` queries the table via `Clock` and repeats temporary errors; `drain_once` allows manual control of the cycle. Rows are not deleted automatically: retention inbox/outbox must consider the maximum window for repeated delivery.

## Migrations

`Migrator` accepts a fully sorted history of `Migration`. For each number, constant SQL commands expand and the optional contract are defined. `@schema@` only substitutes a verified schema identifier. The transaction advisory lock is used for the schema; the applied commands are protected by SHA-256. Changes to the applied script, skipping history, and running contract before expand are rejected.

The CLI `psim-pg-migrate` reads the trusted release manifest:

```json
{
  "schema": "incident_service",
  "migrations": [
    {
      "version": 1,
      "name": "initial",
      "expand": ["CREATE TABLE @schema@.incident (id uuid PRIMARY KEY, version bigint NOT NULL)"],
      "contract": []
    }
  ]
}
```

```bash
psim-pg-migrate expand migrations.json
psim-pg-migrate contract migrations.json
```

Connection parameters: `PSIM_PG_HOST`, `PSIM_PG_PORT`, `PSIM_PG_DATABASE`, `PSIM_PG_USER`, `PSIM_PG_PASSWORD`, `PSIM_PG_SSLMODE`, `PSIM_PG_SSLROOTCERT`, `PSIM_PG_SSLCERT`, `PSIM_PG_SSLKEY`. The secret is transmitted through the process environment, not as a command-line argument.

The contract is executed by the release process after removing incompatible old instances; the tool itself does not define the versions of the running services. The plan is executed transactionally and does not support commands prohibited within a transaction, such as `CREATE INDEX CONCURRENTLY`.

## Verification

```bash
task cpp:test
task env:up
task test:component
task test:pg:sanitizers
task bench:pg
```

Component tests work with PostgreSQL and Kafka from the environment. They test queries and types, errors, pipeline, COPY, notifications, deadlines and revocation, read-only endpoints, connection replacement, transactions, migrations, inbox/outbox, and repeated publication. The working process forcibly terminates with SIGKILL before COMMIT, after COMMIT, and after Kafka confirmation; re-processing is checked through the inbox. `task test:pg:sanitizers` repeats component scenarios from PostgreSQL under ASan/UBSan and TSan, including KIP-848 reading published messages.

`task bench:pg` measures COPY and pipeline throughput and p50/p99 queries. These measurements relate to database access layers; cross-MVP targets are tested in the corresponding system qualification environments.
