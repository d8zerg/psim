#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../../kafka/tests/component_support.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/migrations.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "support.hpp"

namespace psim::platform::pg::testing {

namespace {
asio::awaitable<Result<void>> fake_publish(const bool* should_fail, std::size_t* published,
                                           std::span<const OutboxMessage> messages) {
  *published += messages.size();
  EXPECT_EQ(messages[0].record.value, std::string("a\0b", 3));
  if (*should_fail) {
    co_return fail(ErrorCode::kCommonUnavailable, "broker unavailable");
  }
  co_return Result<void>{};
}

asio::awaitable<Result<void>> publish_large(std::size_t* published, std::span<const OutboxMessage> messages) {
  EXPECT_EQ(messages.size(), 1);
  for (const auto& message : messages) {
    EXPECT_EQ(message.id, std::to_string(*published));
    EXPECT_EQ(message.record.value, std::string(std::size_t{1024} * 1024, 'p'));
    EXPECT_EQ(message.record.key, std::string("a\0b", 3));
    EXPECT_EQ(message.record.partition, 7);
    EXPECT_EQ(message.record.headers,
              (kafka::Headers{{"binary", std::string("\xff\0\xfe", 3)}, {"same", "first"}, {"same", "second"}}));
    ++*published;
  }
  co_return Result<void>{};
}

asio::awaitable<Result<int>> write_repository(Transaction* transaction, std::string schema, bool reject) {
  auto inserted =
      co_await transaction->connection().query({.sql = sql(schema, "INSERT INTO @schema@.items VALUES ($1)"),
                                                .parameters = {Parameter::integer(reject ? 2 : 1)}});
  if (!inserted) {
    co_return std::unexpected(inserted.error());
  }
  MessageStore store(schema);
  const auto appended = co_await store.append(
      transaction, {.id = reject ? "rejected" : "accepted", .record = {.topic = "events", .value = "payload"}});
  if (!appended) {
    co_return std::unexpected(appended.error());
  }
  if (reject) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "domain validation failed");
  }
  co_return 42;
}

}  // namespace

TEST(PgConnection, ParametersPreparedStatementsAndResultMetadata) {
  auto scenario = []() -> asio::awaitable<void> {
    auto connection = co_await connect();
    auto result = take(co_await connection->query(
        {.sql = "SELECT $1::bigint AS number, $2::text, encode($3::bytea, 'hex'), $4::jsonb, $5::boolean, $6::uuid, "
                "$7::text[]",
         .parameters = {Parameter::integer(-42), Parameter::text("'; DROP TABLE ignored; --"),
                        Parameter::bytes(std::string("a\0b", 3)), Parameter::text("{\"ok\":true}"),
                        Parameter::boolean(true), Parameter::null(2950), Parameter::text("{alpha,beta}")}}));
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result.columns(), 7);
    EXPECT_EQ(take(result.integer(0, 0)), -42);
    EXPECT_EQ(result.column_name(0), "number");
    EXPECT_EQ(result.column_type(0), 20);
    EXPECT_EQ(result.value(0, 1), "'; DROP TABLE ignored; --");
    EXPECT_EQ(result.value(0, 2), "610062");
    EXPECT_EQ(result.value(0, 4), "t");
    EXPECT_FALSE(result.value(0, 5));
    EXPECT_EQ(result.value(0, 6), "{alpha,beta}");
    EXPECT_FALSE(result.integer(0, 1));
    check(co_await connection->prepare("sum", "SELECT $1::bigint + $2::bigint", {20, 20}));
    auto sum = take(co_await connection->execute("sum", {Parameter::integer(20), Parameter::integer(22)}));
    EXPECT_EQ(take(sum.integer(0, 0)), 42);
    const Rows moved(std::move(sum));
    EXPECT_EQ(take(moved.integer(0, 0)), 42);
    EXPECT_TRUE(connection->reusable());
  };
  run(scenario());
}

TEST(PgConnection, ErrorsAreRedactedAndQueryValidationDoesNotPoisonTheConnection) {
  auto scenario = []() -> asio::awaitable<void> {
    auto connection = co_await connect();
    auto bad = co_await connection->query({.sql = "SELECT 'private-password'::integer"});
    EXPECT_FALSE(bad);
    EXPECT_EQ(bad.error().message().find("private-password"), std::string::npos);
    EXPECT_NE(bad.error().message().find("22P02"), std::string::npos);
    EXPECT_FALSE(co_await connection->query({}));
    EXPECT_FALSE(co_await connection->query(
        {.sql = "SELECT $1::text", .parameters = {Parameter::text(std::string("a\0b", 3))}}));
    EXPECT_FALSE(co_await connection->prepare("", "SELECT 1"));
    EXPECT_FALSE(co_await connection->execute(""));
    EXPECT_FALSE(co_await connection->pipeline({}));
    const std::string large(std::size_t{9} * 1024 * 1024, 'x');
    EXPECT_FALSE(co_await connection->query(
        {.sql = "SELECT $1::text, $2::text", .parameters = {Parameter::text(large), Parameter::text(large)}}));
    EXPECT_FALSE(
        co_await connection->pipeline({{.sql = "SELECT length($1::text)", .parameters = {Parameter::text(large)}},
                                       {.sql = "SELECT length($1::text)", .parameters = {Parameter::text(large)}}}));
    auto good = take(co_await connection->query({.sql = "SELECT 42"}));
    EXPECT_EQ(take(good.integer(0, 0)), 42);
  };
  run(scenario());
}

TEST(PgConnection, ReadOnlyEndpointIsRejectedBeforeItCanEnterTheWritePool) {
  auto scenario = []() -> asio::awaitable<void> {
    auto config = options();
    config.properties["options"] = "-cdefault_transaction_read_only=on";
    auto rejected = co_await Connection::connect(config);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), ErrorCode::kCommonUnavailable);
    config.properties["target_session_attrs"] = "any";
    auto read_only = take(co_await Connection::connect(config));
    auto mode = take(co_await read_only->query({.sql = "SHOW transaction_read_only"}));
    EXPECT_EQ(mode.value(0, 0), "on");
  };
  run(scenario());
}

TEST(PgConnection, CoroutineCancellationClosesTheConnectionBeforeItCanBeReused) {
  auto scenario = []() -> asio::awaitable<void> {
    auto connection = co_await connect();
    asio::steady_timer timer(co_await asio::this_coro::executor, std::chrono::milliseconds{20});
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator||
    const auto outcome =
        co_await (connection->query({.sql = "SELECT pg_sleep(5)"}) || timer.async_wait(asio::use_awaitable));
    EXPECT_EQ(outcome.index(), 1);
    EXPECT_FALSE(connection->reusable());
    EXPECT_FALSE(co_await connection->query({.sql = "SELECT 1"}));
  };
  run(scenario());
}

TEST(PgConnection, PipelineRollsBackTheBatchOnSqlErrorAndRecoversAtSync) {
  auto scenario = []() -> asio::awaitable<void> {
    auto connection = co_await connect();
    take(co_await connection->query({.sql = "CREATE TEMPORARY TABLE pipeline_test (id integer PRIMARY KEY)"}));
    auto batch = take(co_await connection->pipeline(
        {{.sql = "INSERT INTO pipeline_test VALUES ($1)", .parameters = {Parameter::integer(1)}},
         {.sql = "SELECT count(*) FROM pipeline_test"}}));
    EXPECT_EQ(batch.size(), 2);
    EXPECT_EQ(batch[0].affected(), 1);
    EXPECT_EQ(take(batch[1].integer(0, 0)), 1);
    auto failed = co_await connection->pipeline({{.sql = "INSERT INTO pipeline_test VALUES (2)"},
                                                 {.sql = "SELECT 1 / 0"},
                                                 {.sql = "INSERT INTO pipeline_test VALUES (3)"}});
    EXPECT_FALSE(failed);
    auto count = take(co_await connection->query({.sql = "SELECT count(*) FROM pipeline_test"}));
    EXPECT_EQ(take(count.integer(0, 0)), 1);
    EXPECT_TRUE(connection->reusable());
  };
  run(scenario());
}

TEST(PgConnection, CopyAndNotificationsPreserveData) {
  auto scenario = []() -> asio::awaitable<void> {
    auto connection = co_await connect();
    take(co_await connection->query({.sql = "CREATE TEMPORARY TABLE copy_test (id integer, value text)"}));
    check(co_await connection->copy_in("COPY copy_test (id, value) FROM STDIN", {"1\talpha\n", "2\tbeta\n"}));
    auto count = take(co_await connection->query({.sql = "SELECT count(*) FROM copy_test"}));
    EXPECT_EQ(take(count.integer(0, 0)), 2);
    take(co_await connection->query({.sql = "LISTEN pg_test_channel"}));
    auto sender = co_await connect();
    take(co_await sender->query({.sql = "SELECT pg_notify($1, $2)",
                                 .parameters = {Parameter::text("pg_test_channel"), Parameter::text("ready")}}));
    auto notice = take(co_await connection->notification(std::chrono::seconds{1}));
    EXPECT_EQ(notice.channel, "pg_test_channel");
    EXPECT_EQ(notice.payload, "ready");
  };
  run(scenario());
}

TEST(PgPool, DeadlineDiscardsPoisonedConnectionAndCreatesAReplacement) {
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options(), .size = 1, .acquire_timeout = std::chrono::milliseconds{100}});
    int previous_pid = 0;
    {
      auto lease = take(co_await pool.acquire());
      previous_pid = lease->backend_pid();
      EXPECT_FALSE(co_await pool.acquire(std::chrono::milliseconds{10}));
      auto slept = co_await lease->query({.sql = "SELECT pg_sleep(5)"}, std::chrono::milliseconds{20});
      EXPECT_FALSE(slept);
      EXPECT_EQ(slept.error().code(), ErrorCode::kCommonDeadlineExceeded);
      EXPECT_FALSE(lease->reusable());
    }
    EXPECT_EQ(pool.idle(), 0);
    {
      auto lease = take(co_await pool.acquire());
      EXPECT_NE(lease->backend_pid(), previous_pid);
      take(co_await lease->query({.sql = "SELECT 1"}));
    }
    EXPECT_EQ(pool.idle(), 1);
    pool.close();
    EXPECT_FALSE(co_await pool.acquire());
  };
  run(scenario());
}

TEST(PgTransaction, CommitRollbackAbandonmentAndAbortedCommitHaveDistinctOutcomes) {
  auto scenario = []() -> asio::awaitable<void> {
    auto connection = co_await connect();
    take(co_await connection->query({.sql = "CREATE TEMPORARY TABLE tx_test (id integer PRIMARY KEY)"}));
    auto transaction = take(co_await Transaction::begin(connection.get(), Isolation::kSerializable));
    EXPECT_FALSE(connection->reusable());
    EXPECT_FALSE(co_await Transaction::begin(connection.get()));
    take(co_await connection->query({.sql = "INSERT INTO tx_test VALUES (1)"}));
    check(co_await transaction.commit());
    EXPECT_FALSE(co_await transaction.commit());
    auto aborted = take(co_await Transaction::begin(connection.get(), Isolation::kRepeatableRead));
    EXPECT_FALSE(co_await connection->query({.sql = "INSERT INTO tx_test VALUES (1)"}));
    EXPECT_FALSE(co_await aborted.commit());
    EXPECT_TRUE(connection->reusable());
    auto rollback = take(co_await Transaction::begin(connection.get()));
    take(co_await connection->query({.sql = "INSERT INTO tx_test VALUES (2)"}));
    check(co_await rollback.rollback());
    EXPECT_FALSE(co_await rollback.rollback());
    const auto count = take(co_await connection->query({.sql = "SELECT count(*) FROM tx_test"}));
    EXPECT_EQ(take(count.integer(0, 0)), 1);
    {
      auto abandoned = take(co_await Transaction::begin(connection.get()));
    }
    EXPECT_FALSE(connection->reusable());
  };
  run(scenario());
}

TEST(PgMigrations, ExpandContractAreSeparateIdempotentAndDetectEditedHistory) {
  const auto schema = unique_schema();
  auto scenario = [](std::string schema) -> asio::awaitable<void> {
    auto connection = co_await connect();
    Migrator migrator(schema);
    std::vector<Migration> plan{
        {.version = 1,
         .name = "initial",
         .expand = {"CREATE TABLE @schema@.items (id integer, old_value text)"},
         .contract = {"ALTER TABLE @schema@.items DROP COLUMN old_value"}},
        {.version = 2, .name = "addition", .expand = {"ALTER TABLE @schema@.items ADD COLUMN value text"}}};
    check(co_await migrator.apply(connection.get(), plan));
    check(co_await migrator.apply(connection.get(), plan));
    take(co_await connection->query(
        {.sql = sql(schema, "INSERT INTO @schema@.items (id, old_value, value) VALUES ($1, $2, $3)"),
         .parameters = {Parameter::integer(1), Parameter::text("old"), Parameter::text("new")}}));
    check(co_await migrator.apply(connection.get(), plan, MigrationPhase::kContract));
    check(co_await migrator.apply(connection.get(), plan, MigrationPhase::kContract));
    auto row = take(co_await connection->query({.sql = sql(schema, "SELECT id, value FROM @schema@.items")}));
    EXPECT_EQ(row.value(0, 1), "new");
    plan[0].expand = {"CREATE TABLE @schema@.items (id text)"};
    EXPECT_FALSE(co_await migrator.apply(connection.get(), plan));
    co_await drop(schema);
  };
  run(scenario(schema));
}

TEST(PgMigrations, ProvisionedSchemaNeedsNoDatabaseCreatePrivilege) {
  auto scenario = [](std::string schema) -> asio::awaitable<void> {
    auto privileged = options();
    privileged.user = "postgres";
    privileged.password = "psim-dev-postgres";
    auto connection = take(co_await Connection::connect(std::move(privileged)));
    const auto role = schema + "_role";
    take(co_await connection->query({.sql = "CREATE ROLE " + role + " NOLOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE"}));
    take(co_await connection->query({.sql = "CREATE SCHEMA " + schema + " AUTHORIZATION " + role}));
    take(co_await connection->query({.sql = "SET ROLE " + role}));
    auto privileges = take(co_await connection->query(
        {.sql = "SELECT has_database_privilege(current_user, current_database(), 'CREATE')"}));
    EXPECT_EQ(privileges.value(0, 0), "f");
    Migrator migrator(schema);
    std::vector<Migration> plan{{.version = 1, .name = "items", .expand = {"CREATE TABLE @schema@.items (id bigint)"}}};
    check(co_await migrator.apply(connection.get(), plan));
    check(co_await migrator.apply(connection.get(), plan));
    MessageStore store(schema);
    check(co_await store.initialize(connection.get()));
    check(co_await store.initialize(connection.get()));
    EXPECT_FALSE(co_await ensure_schema(connection.get(), schema + "_forbidden"));
    take(co_await connection->query({.sql = "RESET ROLE"}));
    take(co_await connection->query({.sql = "DROP SCHEMA " + schema + " CASCADE"}));
    take(co_await connection->query({.sql = "DROP ROLE " + role}));
  };
  run(scenario(unique_schema()));
}

TEST(PgStore, InboxOutboxAndDomainStateCommitTogetherAndRejectIdentityReuse) {
  const auto schema = unique_schema();
  auto scenario = [](std::string schema) -> asio::awaitable<void> {
    auto connection = co_await connect();
    MessageStore store(schema);
    check(co_await store.initialize(connection.get()));
    check(co_await store.initialize(connection.get()));
    auto transaction = take(co_await Transaction::begin(connection.get()));
    EXPECT_TRUE(take(co_await store.accept(&transaction, "handler", "event", "hash")));
    check(co_await store.append(&transaction,
                                {.id = "event", .record = {.topic = "events", .key = "key", .value = "payload"}}));
    check(co_await transaction.rollback());
    auto repeat = take(co_await Transaction::begin(connection.get()));
    EXPECT_TRUE(take(co_await store.accept(&repeat, "handler", "event", "hash")));
    check(co_await store.append(&repeat, {.id = "event", .record = {.topic = "events", .value = "payload"}}));
    check(co_await repeat.commit());
    auto duplicate = take(co_await Transaction::begin(connection.get()));
    EXPECT_FALSE(take(co_await store.accept(&duplicate, "handler", "event", "hash")));
    EXPECT_FALSE(co_await store.accept(&duplicate, "handler", "event", "different"));
    check(co_await store.append(&duplicate, {.id = "event", .record = {.topic = "events", .value = "payload"}}));
    EXPECT_FALSE(co_await store.append(&duplicate, {.id = "event", .record = {.topic = "events", .value = "changed"}}));
    check(co_await duplicate.rollback());
    co_await drop(schema);
  };
  run(scenario(schema));
}

TEST(PgOutbox, FailedPublisherLeavesTheWholeBatchPendingAndSuccessfulRetryDrainsIt) {
  const auto schema = unique_schema();
  auto scenario = [](std::string schema) -> asio::awaitable<void> {
    auto connection = co_await connect();
    MessageStore store(schema);
    check(co_await store.initialize(connection.get()));
    auto transaction = take(co_await Transaction::begin(connection.get()));
    for (int i = 0; i < 50; ++i) {
      check(co_await store.append(
          &transaction,
          {.id = std::to_string(i), .record = {.topic = "events", .key = "key", .value = std::string("a\0b", 3)}}));
    }
    check(co_await transaction.commit());
    Pool pool({.connection = options()});
    bool fail_publish = true;
    std::size_t published = 0;
    const Publisher publish = [&fail_publish, &published](std::span<const OutboxMessage> messages) {
      return fake_publish(&fail_publish, &published, messages);
    };
    OutboxRelay relay(&pool, store, publish, 20);
    EXPECT_FALSE(co_await relay.drain_once());
    fail_publish = false;
    EXPECT_EQ(take(co_await relay.drain_once()), 20);
    EXPECT_EQ(take(co_await relay.drain_once()), 20);
    EXPECT_EQ(take(co_await relay.drain_once()), 10);
    EXPECT_EQ(take(co_await relay.drain_once()), 0);
    EXPECT_EQ(published, 70);
    co_await drop(schema);
  };
  run(scenario(schema));
}

TEST(PgConnection, TerminatedBackendFailsWithoutRetryingAndPoolReconnects) {
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options(), .size = 1});
    {
      auto lease = take(co_await pool.acquire());
      auto control = co_await connect();
      take(co_await control->query({.sql = "SELECT pg_terminate_backend($1::integer)",
                                    .parameters = {Parameter::integer(lease->backend_pid())}}));
      EXPECT_FALSE(co_await lease->query({.sql = "SELECT 1"}));
      EXPECT_FALSE(lease->reusable());
    }
    auto lease = take(co_await pool.acquire());
    EXPECT_TRUE(co_await lease->query({.sql = "SELECT 1"}));
  };
  run(scenario());
}

TEST(PgConnection, ProxyDisconnectionDiscardsTheLeaseAndReconnectsAfterRecovery) {
  auto scenario = []() -> asio::awaitable<void> {
    Pool pool({.connection = options(true), .size = 1});
    {
      auto lease = take(co_await pool.acquire());
      kafka::testing::toxiproxy_enable("postgres", false);
      auto disconnected = co_await lease->query({.sql = "SELECT 1"});
      kafka::testing::toxiproxy_enable("postgres", true);
      EXPECT_FALSE(disconnected);
      EXPECT_FALSE(lease->reusable());
    }
    auto recovered = take(co_await pool.acquire());
    EXPECT_TRUE(co_await recovered->query({.sql = "SELECT 1"}));
  };
  run(scenario());
}

TEST(PgOutbox, LargeMessagesDrainInByteBoundedBatchesAndPreserveKafkaMetadata) {
  const auto schema = unique_schema();
  auto scenario = [](std::string schema) -> asio::awaitable<void> {
    auto connection = co_await connect();
    MessageStore store(schema);
    check(co_await store.initialize(connection.get()));
    auto transaction = take(co_await Transaction::begin(connection.get()));
    for (int i = 0; i < 10; ++i) {
      check(co_await store.append(
          &transaction,
          {.id = std::to_string(i),
           .record = {.topic = "events",
                      .key = std::string("a\0b", 3),
                      .value = std::string(std::size_t{1024} * 1024, 'p'),
                      .headers = {{"binary", std::string("\xff\0\xfe", 3)}, {"same", "first"}, {"same", "second"}},
                      .partition = 7}}));
    }
    EXPECT_FALSE(co_await store.append(
        &transaction,
        {.id = "too-large", .record = {.topic = "events", .value = std::string((std::size_t{1024} * 1024) + 1, 'p')}}));
    EXPECT_FALSE(
        co_await store.append(&transaction, {.id = "bad-topic", .record = {.topic = "bad topic", .value = "p"}}));
    check(co_await transaction.commit());
    Pool pool({.connection = options()});
    std::size_t published = 0;
    const Publisher publisher = [&published](std::span<const OutboxMessage> messages) {
      return publish_large(&published, messages);
    };
    OutboxRelay relay(&pool, store, publisher);
    for (int i = 0; i < 10; ++i) {
      EXPECT_EQ(take(co_await relay.drain_once()), 1);
    }
    EXPECT_EQ(take(co_await relay.drain_once()), 0);
    EXPECT_EQ(published, 10);
    co_await drop(schema);
  };
  run(scenario(schema));
}

TEST(PgRepository, UnitOfWorkCommitsDomainAndOutboxTogetherAndRollsBackCallbackErrors) {
  const auto schema = unique_schema();
  auto scenario = [](std::string schema) -> asio::awaitable<void> {
    auto connection = co_await connect();
    MessageStore store(schema);
    check(co_await store.initialize(connection.get()));
    take(co_await connection->query({.sql = sql(schema, "CREATE TABLE @schema@.items (id integer)")}));
    Pool pool({.connection = options(), .size = 1});
    EXPECT_EQ(take(co_await in_transaction<int>(
                  &pool, [schema](Transaction* transaction) { return write_repository(transaction, schema, false); })),
              42);
    auto rejected = co_await in_transaction<int>(
        &pool, [schema](Transaction* transaction) { return write_repository(transaction, schema, true); });
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), ErrorCode::kCommonInvalidArgument);
    auto counts = take(co_await connection->query(
        {.sql = sql(schema,
                    "SELECT (SELECT count(*) FROM @schema@.items), (SELECT count(*) FROM @schema@.psim_outbox)")}));
    EXPECT_EQ(take(counts.integer(0, 0)), 1);
    EXPECT_EQ(take(counts.integer(0, 1)), 1);
    EXPECT_EQ(pool.idle(), 1);
    co_await drop(schema);
  };
  run(scenario(schema));
}

TEST(PgOutbox, KafkaPublisherRejectsAnOpenBrokerTransactionAndKeepsTheRowPending) {
  const auto schema = unique_schema();
  kafka::testing::Topics topics;
  const auto topic = topics.create("pg.publisher", 1);
  auto scenario = [](std::string schema, std::string topic) -> asio::awaitable<void> {
    auto connection = co_await connect();
    MessageStore store(schema);
    check(co_await store.initialize(connection.get()));
    auto transaction = take(co_await Transaction::begin(connection.get()));
    check(co_await store.append(&transaction, {.id = "event", .record = {.topic = topic, .value = "payload"}}));
    check(co_await transaction.commit());
    async::BlockingPool blocking(1, 4);
    auto producer = take(kafka::Producer::create(co_await asio::this_coro::executor,
                                                 {.client = kafka::testing::client(), .transactional_id = schema},
                                                 {.blocking = &blocking}));
    check(co_await producer->init_transactions(std::chrono::seconds{30}));
    check(producer->begin_transaction());
    Pool pool({.connection = options()});
    OutboxRelay relay(&pool, store, kafka_publisher(producer.get()));
    auto rejected = co_await relay.drain_once();
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), ErrorCode::kCommonInvalidArgument);
    auto pending = take(co_await connection->query(
        {.sql = sql(schema, "SELECT count(*) FROM @schema@.psim_outbox WHERE published_at IS NULL")}));
    EXPECT_EQ(take(pending.integer(0, 0)), 1);
    EXPECT_TRUE(producer->in_transaction());
    check(co_await producer->abort_transaction(std::chrono::seconds{30}));
    co_await drop(schema);
  };
  run(scenario(schema, topic));
}
}  // namespace psim::platform::pg::testing
