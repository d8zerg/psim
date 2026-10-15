#include <boost/asio/this_coro.hpp>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../src/detail.hpp"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "support.hpp"

namespace {
namespace ch = psim::platform::clickhouse;
namespace kafka = psim::platform::kafka;
namespace asio = boost::asio;
namespace ch_testing = ch::testing;

asio::awaitable<void> work(std::string table, std::string topic, std::string dlq, std::string group, int stage) {
  psim::platform::async::BlockingPool blocking(3, 16);
  ch::Pool pool(&blocking, ch_testing::options());
  psim::platform::pg::Pool journal(ch_testing::journal_options());
  auto consumer = ch_testing::take(kafka::Consumer::create(
      co_await asio::this_coro::executor,
      {.client = {.bootstrap = ch_testing::environment("PSIM_TEST_KAFKA"),
                  .client_id = "ch-crash-worker",
                  .properties = {{"session.timeout.ms", "6000"}, {"heartbeat.interval.ms", "1000"}},
                  .allow_plaintext = true},
       .group_id = group,
       .topics = {topic},
       .group_protocol = "classic"},
      {.blocking = &blocking}));
  auto producer =
      ch_testing::take(kafka::Producer::create(co_await asio::this_coro::executor,
                                               {.client = {.bootstrap = ch_testing::environment("PSIM_TEST_KAFKA"),
                                                           .client_id = "ch-crash-worker",
                                                           .allow_plaintext = true},
                                                .transactional_id = group},
                                               {.blocking = &blocking}));
  ch_testing::check(co_await producer->init_transactions(ch::Duration(30000)));
  ch::Loader loader(&pool, &journal, consumer.get(), producer.get(),
                    {.name = "events_v1",
                     .table = table,
                     .dlq = dlq,
                     .max_messages = stage < 0 ? 3U : 26U,
                     .flush_after = ch::Duration(500)},
                    ch_testing::convert, nullptr, [stage](ch::LoadStage point) {
                      if (static_cast<int>(point) == stage) {
                        (void)std::raise(SIGSTOP);  // NOLINT(misc-include-cleaner): POSIX checkpoint signal
                      }
                    });
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (std::chrono::steady_clock::now() < end) {
    ch_testing::take(co_await loader.run_once());
    if (consumer->assignment().empty()) {
      continue;
    }
    const auto committed = ch_testing::take(co_await consumer->committed({{topic, 0}}, ch::Duration(5000)));
    if (committed.contains({topic, 0}) && committed.at({topic, 0}) == 26) {
      auto lease = ch_testing::take(co_await journal.acquire());
      auto rows = ch_testing::take(
          co_await lease->query({.sql = "SELECT count(*) FROM psim_clickhouse.pending_batches WHERE loader=$1",
                                 .parameters = {psim::platform::pg::Parameter::text(group + "/events_v1")}}));
      if (ch_testing::take(rows.integer(0, 0)) == 0) {
        co_await consumer->close();
        co_return;
      }
    }
  }
  throw std::runtime_error("ClickHouse loader recovery timed out");
}

asio::awaitable<void> replica_probe(std::string table, std::string phase) {
  psim::platform::async::BlockingPool blocking(2, 8);
  auto config = ch_testing::options();
  config.size = 1;
  if (phase == "write") {
    config.endpoints.push_back({ch_testing::environment("PSIM_TEST_CLICKHOUSE_SECOND"), 9440});
  }
  ch::Pool pool(&blocking, config);
  if (phase == "write") {
    if (co_await pool.execute_ddl("SELECT 1")) {
      throw std::runtime_error("primary replica was expected to be stopped");
    }
    ch_testing::check(co_await pool.insert(table, ch_testing::block(), ch::detail::fingerprint(table)));
  } else {
    ch_testing::check(co_await pool.execute_ddl("SYSTEM SYNC REPLICA " + table));
  }
  const auto rows = ch_testing::take(co_await pool.select(ch_testing::select(table, false)));
  if (rows.rows != 2) {
    throw std::runtime_error("replica recovery row reconciliation failed");
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 4) {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (std::string_view(arguments[1]) != "replica" || !ch::detail::identifier(arguments[2])
        || (std::string_view(arguments[3]) != "write" && std::string_view(arguments[3]) != "verify")) {
      return 2;
    }
    try {
      ch_testing::run(replica_probe(arguments[2], arguments[3]));
      return 0;
    } catch (const std::exception& error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
  if (argc != 6) {
    return 2;
  }
  try {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    ch_testing::run(work(arguments[1], arguments[2], arguments[3], arguments[4], std::stoi(arguments[5])));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
