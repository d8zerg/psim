#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "support.hpp"

namespace {
namespace pg = psim::platform::pg;
namespace kafka = psim::platform::kafka;
namespace pg_testing = pg::testing;
namespace asio = boost::asio;

asio::awaitable<void> checkpoint(std::string marker) {
  std::ofstream(marker).put('1');
  asio::steady_timer timer(co_await asio::this_coro::executor, std::chrono::minutes(3));
  co_await timer.async_wait(asio::use_awaitable);
}

asio::awaitable<psim::platform::Result<void>> publish_checkpoint(pg::Publisher publish, std::string stage,
                                                                 std::string marker,
                                                                 std::span<const pg::OutboxMessage> messages) {
  auto result = co_await publish(messages);
  if (result && stage == "after_publish") {
    co_await checkpoint(std::move(marker));
  }
  co_return result;
}

asio::awaitable<void> work(std::string schema, std::string topic, std::string stage, std::string marker) {
  pg::MessageStore store(schema);
  auto connection = co_await pg_testing::connect();
  auto transaction = pg_testing::take(co_await pg::Transaction::begin(connection.get()));
  for (int i = 0; i < 25; ++i) {
    const auto id = std::to_string(i);
    if (pg_testing::take(co_await store.accept(&transaction, "writer", id, "payload-" + id))) {
      pg_testing::take(
          co_await connection->query({.sql = pg_testing::sql(schema, "INSERT INTO @schema@.effects (id) VALUES ($1)"),
                                      .parameters = {pg::Parameter::text(id)}}));
      pg_testing::check(co_await store.append(
          &transaction, {.id = id, .record = {.topic = topic, .key = id, .value = "payload-" + id}}));
    }
  }
  if (stage == "before_commit") {
    co_await checkpoint(marker);
  }
  pg_testing::check(co_await transaction.commit());
  if (stage == "after_commit") {
    co_await checkpoint(marker);
  }
  auto producer = pg_testing::take(kafka::Producer::create(
      co_await asio::this_coro::executor, {.client = {.bootstrap = pg_testing::environment("PSIM_TEST_KAFKA"),
                                                      .client_id = "pg-crash-worker",
                                                      .allow_plaintext = true}}));
  const auto publish = pg::kafka_publisher(producer.get());
  const pg::Publisher checkpoint_publish = [publish, stage, marker](std::span<const pg::OutboxMessage> messages) {
    return publish_checkpoint(publish, stage, marker, messages);
  };
  pg::Pool pool({.connection = pg_testing::options()});
  pg::OutboxRelay relay(&pool, store, checkpoint_publish);
  while (pg_testing::take(co_await relay.drain_once()) != 0) {
  }
}
}  // namespace

int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  if (args.size() != 5) {
    return 2;
  }
  try {
    pg_testing::run(work(args[1], args[2], args[3], args[4]));
    return 0;
  } catch (const std::exception&) {
    return 1;
  }
}
