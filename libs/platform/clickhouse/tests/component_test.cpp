#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "../../kafka/tests/component_support.hpp"
#include "../src/detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/deadline.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/processing/v1/event.pb.h"
#include "support.hpp"

namespace psim::platform::clickhouse::testing {
namespace {
std::string unique() {
  return "chtest_" + detail::fingerprint(kafka::testing::unique("ch")).substr(0, 20);
}
}  // namespace

TEST(ClickHouseConnection, NativeTlsBinaryParametersTenantScopeAndDeduplication) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    Pool pool(&blocking, options());
    const auto table = unique();
    co_await create(&pool, table);
    const auto token = detail::fingerprint("batch-1");
    check(co_await pool.insert(table, block(), token));
    check(co_await pool.insert(table, block(), token));
    check(co_await pool.insert(table, block(3, "tenant-b"), detail::fingerprint("batch-2")));
    EXPECT_EQ(take(co_await pool.select(select(table, false))).rows, 2);
    auto query = select(table);
    query.equals = {{"value", std::string("a\0b", 3)}};
    EXPECT_EQ(take(co_await pool.select(query)).rows, 2);
    query.equals = {{"event_id", "' OR 1=1 --"}};
    EXPECT_EQ(take(co_await pool.select(query)).rows, 0);
    query = select(table);
    query.to_ms = 1000;
    EXPECT_EQ(take(co_await pool.select(query)).rows, 0);
    query.tenant = "tenant-b";
    query.to_ms = 2000;
    EXPECT_EQ(take(co_await pool.select(query)).rows, 3);
    co_await drop(&pool, table);
  };
  run(scenario());
}

TEST(ClickHouseConnection, TlsRejectsForeignCaWrongHostnameAndMissingClientCertificate) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    auto config = options();
    config.tls.ca_file.clear();
    Pool foreign(&blocking, config);
    EXPECT_FALSE(co_await foreign.execute_ddl("SELECT 1"));
    config = options();
    config.endpoints = {{"clickhouse-wrong", 9440}};
    Pool wrong(&blocking, config);
    EXPECT_FALSE(co_await wrong.execute_ddl("SELECT 1"));
    config = options();
    config.tls.certificate_file.clear();
    config.tls.private_key_file.clear();
    Pool absent(&blocking, config);
    EXPECT_FALSE(co_await absent.execute_ddl("SELECT 1"));
    config = options();
    config.password_file = "/repo/.cache/clickhouse-tls/reader.password";
    Pool bad_password(&blocking, config);
    const auto failed = co_await bad_password.execute_ddl("SELECT 1");
    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.error().message().find("psim-dev"), std::string::npos);
  };
  run(scenario());
}

TEST(ClickHouseConnection, LoaderAndReaderHaveMinimalDatabaseGrants) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    Pool admin(&blocking, options());
    Pool loader(&blocking, options("psim_ch_loader", "loader"));
    Pool reader(&blocking, options("psim_ch_reader", "reader"));
    const auto table = unique();
    co_await create(&admin, table);
    check(co_await loader.insert(table, block(), detail::fingerprint("loader")));
    EXPECT_EQ(take(co_await reader.select(select(table))).rows, 2);
    EXPECT_FALSE(co_await reader.insert(table, block(), detail::fingerprint("reader")));
    // psim-arch: allow-sql (validated identifiers or generated test names only)
    EXPECT_FALSE(co_await loader.execute_ddl("DROP TABLE " + table));
    co_await drop(&admin, table);
  };
  run(scenario());
}

TEST(ClickHouseConnection, QueryResultAndTimeLimitsFailWithoutPartialSuccess) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    Pool admin(&blocking, options());
    const auto table = unique();
    co_await create(&admin, table);
    check(co_await admin.insert(table, block(100), detail::fingerprint("large")));
    auto config = options();
    config.max_result_bytes = 100;
    Pool bounded(&blocking, config);
    EXPECT_FALSE(co_await bounded.select(select(table)));
    config = options();
    config.query_timeout = Duration(1000);
    config.socket_timeout = Duration(1500);
    Pool deadline(&blocking, config);
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(co_await deadline.execute_ddl("SELECT sleep(2)"));
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
    EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::seconds(1));
    co_await drop(&admin, table);
  };
  run(scenario());
}

TEST(ClickHouseConnection, AdmissionDeadlineAndCancellationDrainNativeIo) {
  auto scenario = []() -> asio::awaitable<void> {
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): coroutine
                                                              // composition
    async::BlockingPool blocking(2, 8);
    auto config = options();
    config.size = 1;
    config.acquire_timeout = Duration(20);
    Pool pool(&blocking, config);
    auto waiting = [](Pool* connection) -> asio::awaitable<void> {
      asio::steady_timer timer(co_await asio::this_coro::executor, Duration(50));
      co_await timer.async_wait(asio::use_awaitable);
      const auto result = co_await connection->execute_ddl("SELECT 1");
      EXPECT_FALSE(result);
      if (!result) {
        EXPECT_EQ(result.error().code(), ErrorCode::kCommonDeadlineExceeded);
      }
    };
    auto outcome = co_await (pool.execute_ddl("SELECT sleep(0.2)") && waiting(&pool));
    check(outcome);
    async::SystemClock clock;
    auto cancelled = co_await async::with_deadline(&clock, Duration(20), pool.execute_ddl("SELECT sleep(0.2)"));
    EXPECT_FALSE(cancelled);
    check(co_await pool.execute_ddl("SELECT 1"));
  };
  run(scenario());
}

TEST(ClickHouseConnection, FailedReplicaRotatesAndRecoveredReplicaCatchesUp) {
  struct ProxyReset {
    ProxyReset() = default;
    ProxyReset(const ProxyReset&) = delete;
    ProxyReset& operator=(const ProxyReset&) = delete;
    ProxyReset(ProxyReset&&) = delete;
    ProxyReset& operator=(ProxyReset&&) = delete;

    ~ProxyReset() {
      try {
        kafka::testing::toxiproxy_enable("clickhouse-tls", true);
      } catch (...) {
        ADD_FAILURE() << "cannot restore ClickHouse proxy";
      }
    }
  };

  const ProxyReset reset;

  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    Pool admin(&blocking, options());
    const auto table = unique();
    co_await create(&admin, table);
    auto config = options();
    config.size = 1;
    config.endpoints = {{environment("PSIM_TEST_CLICKHOUSE_FAULT"), 19440},
                        {environment("PSIM_TEST_CLICKHOUSE_SECOND"), 9440}};
    config.socket_timeout = Duration(1000);
    Pool pool(&blocking, config);
    check(co_await pool.insert(table, block(), detail::fingerprint("before-failure")));
    kafka::testing::toxiproxy_enable("clickhouse-tls", false);
    EXPECT_FALSE(co_await pool.select(select(table)));
    check(co_await pool.insert(table, block(2, "tenant-a", 2), detail::fingerprint("during-failure")));
    kafka::testing::toxiproxy_enable("clickhouse-tls", true);
    check(co_await admin.execute_ddl("SYSTEM SYNC REPLICA " + table));
    EXPECT_EQ(take(co_await admin.select(select(table, false))).rows, 4);
    co_await drop(&admin, table);
  };
  run(scenario());
}

TEST(ClickHouseMigrations, OldReaderAcceptsCompatibleExpansionButRejectsContract) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    Pool pool(&blocking, options());
    pg::Pool journal(journal_options());
    const auto table = unique();
    Migrator runner(&pool, &journal, table);
    const std::vector<Migration> old{
        {.version = 1,
         .name = "initial",
         // psim-arch: allow-sql (generated test identifier only)
         .expand = {"CREATE TABLE IF NOT EXISTS " + table + " (id UInt64) ENGINE=Memory"}}};
    check(co_await runner.apply(old));
    auto next = old;
    next.push_back({.version = 2,
                    .name = "compatible",
                    // psim-arch: allow-sql (generated test identifier only)
                    .expand = {"ALTER TABLE " + table + " ADD COLUMN IF NOT EXISTS note Nullable(String)"},
                    // psim-arch: allow-sql (generated test identifier only)
                    .contract = {"ALTER TABLE " + table + " DROP COLUMN IF EXISTS note"},
                    .minimum_reader = 1});
    check(co_await runner.apply(next));
    check(co_await runner.apply(old));
    next[1].minimum_reader = 2;
    EXPECT_FALSE(co_await runner.apply(next));
    next[1].minimum_reader = 1;
    check(co_await runner.apply(next, MigrationPhase::kContract));
    EXPECT_FALSE(co_await runner.apply(old));
    check(co_await pool.execute_ddl("DROP TABLE " + table));  // psim-arch: allow-sql (generated test identifier only)
    auto lease = take(co_await journal.acquire());
    take(co_await lease->query({.sql = "DELETE FROM psim_clickhouse.schema_migrations WHERE target=$1",
                                .parameters = {pg::Parameter::text(table)}}));
  };
  run(scenario());
}

TEST(ClickHouseMigrations, ExpandContractResumeAndChecksumValidation) {
  auto scenario = []() -> asio::awaitable<void> {
    async::BlockingPool blocking(2, 8);
    Pool pool(&blocking, options());
    pg::Pool journal(journal_options());
    const auto table = unique();
    Migrator runner(&pool, &journal, table);
    std::vector<Migration> migrations{
        {.version = 1,
         .name = "table",
         // psim-arch: allow-sql (validated identifiers or generated test names only)
         .expand = {"CREATE TABLE IF NOT EXISTS " + table
                        + " ON CLUSTER psim_clickhouse (id UInt64, obsolete String) "
                          "ENGINE=ReplicatedMergeTree('/clickhouse/ch-component/{shard}/{database}/"
                        + table + "', '{replica}') ORDER BY id",
                    // psim-arch: allow-sql (generated test identifier only)
                    "ALTER TABLE " + table + " ON CLUSTER psim_clickhouse ADD COLUMN IF NOT EXISTS value String"},
         // psim-arch: allow-sql (validated identifiers or generated test names only)
         .contract = {"ALTER TABLE " + table + " ON CLUSTER psim_clickhouse DROP COLUMN IF EXISTS obsolete"}}};
    EXPECT_FALSE(co_await runner.apply(migrations, MigrationPhase::kContract));
    check(co_await runner.apply(migrations));
    check(co_await runner.apply(migrations));
    check(co_await runner.apply(migrations, MigrationPhase::kContract));
    check(co_await runner.apply(migrations, MigrationPhase::kContract));
    migrations[0].expand.emplace_back("SELECT 1");
    EXPECT_FALSE(co_await runner.apply(migrations));
    co_await drop(&pool, table);
    auto lease = take(co_await journal.acquire());
    take(co_await lease->query({.sql = "DELETE FROM psim_clickhouse.schema_migrations WHERE target=$1",
                                .parameters = {pg::Parameter::text(table)}}));
  };
  run(scenario());
}

TEST(ClickHouseLoader, ProtobufColumnsTransactionalDlqAndJournalCleanup) {
  kafka::testing::Topics topics;
  const auto source = topics.create("ch.input", 2);
  const auto dlq = topics.create("ch.dlq", 1);
  auto scenario = [](std::string source, std::string dlq) -> asio::awaitable<void> {
    async::BlockingPool blocking(3, 16);
    Pool pool(&blocking, options());
    pg::Pool journal(journal_options());
    const auto table = unique();
    co_await create(&pool, table);
    check(co_await Loader::initialize(&journal));
    auto producer =
        take(kafka::Producer::create(co_await asio::this_coro::executor, {.client = kafka::testing::client()}));
    const kafka::ProtobufSerde<psim::processing::v1::EventRecord> serde(1);
    for (int index = 0; index < 25; ++index) {
      psim::processing::v1::EventRecord record;
      record.mutable_envelope()->set_tenant_id(index % 2 == 0 ? "tenant-a" : "tenant-b");
      record.mutable_envelope()->set_message_id(std::to_string(index));
      record.mutable_envelope()->mutable_occurred_at()->set_seconds(1);
      record.mutable_event()->set_raw_code(std::string("a\0b", 3));
      check(co_await producer->enqueue(
          {.topic = source, .key = std::to_string(index), .value = serde.serialize(record), .partition = index % 2}));
    }
    check(co_await producer->enqueue(
        {.topic = source, .key = "bad", .value = std::string("\xff\0bad", 5), .partition = 0}));
    check(co_await producer->flush(Duration(30000)));
    const auto group = kafka::testing::unique("ch.group");
    auto consumer = take(kafka::Consumer::create(
        co_await asio::this_coro::executor, {.client = kafka::testing::client(), .group_id = group, .topics = {source}},
        {.blocking = &blocking}));
    auto output = take(kafka::Producer::create(co_await asio::this_coro::executor,
                                               {.client = kafka::testing::client(), .transactional_id = group},
                                               {.blocking = &blocking}));
    check(co_await output->init_transactions(Duration(30000)));
    Loader loader(&pool, &journal, consumer.get(), output.get(),
                  {.name = "events_v1", .table = table, .dlq = dlq, .max_messages = 7}, convert);
    std::size_t processed = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (processed < 26 && std::chrono::steady_clock::now() < end) {
      processed += take(co_await loader.run_once());
    }
    EXPECT_EQ(processed, 26);
    EXPECT_EQ(take(co_await pool.select(select(table, false))).rows, 13);
    auto other = select(table);
    other.tenant = "tenant-b";
    EXPECT_EQ(take(co_await pool.select(other)).rows, 12);
    auto lease = take(co_await journal.acquire());
    auto pending =
        take(co_await lease->query({.sql = "SELECT count(*) FROM psim_clickhouse.pending_batches WHERE loader=$1",
                                    .parameters = {pg::Parameter::text(group + "/events_v1")}}));
    EXPECT_EQ(take(pending.integer(0, 0)), 0);
    co_await consumer->close();
    co_await drop(&pool, table);
  };
  run(scenario(source, dlq));
  const auto rejected = kafka::testing::read_all(dlq, 1, std::chrono::seconds(15), true);
  ASSERT_EQ(rejected.size(), 1);
  EXPECT_EQ(rejected[0].second, std::string("\xff\0bad", 5));
}
}  // namespace psim::platform::clickhouse::testing
