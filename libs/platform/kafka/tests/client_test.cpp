// Producer and consumer without a broker: configuration, delivery failures, timeouts, misuse.
// The paths that need Kafka are covered by the component tests (TS-03).

#include "psim/platform/kafka/client.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <gtest/gtest.h>
#include <prometheus/registry.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/metrics.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "support.hpp"

namespace psim::platform::kafka {
namespace {

using std::chrono::milliseconds;
using testing::run;

// Nothing listens on port 1: connections are refused at once.
constexpr auto kNowhere = "127.0.0.1:1";

ProducerOptions nowhere(milliseconds delivery_timeout = milliseconds(300)) {
  return {.client = {.bootstrap = kNowhere, .client_id = "psim-test", .properties = {}},
          .transactional_id = {},
          .delivery_timeout = delivery_timeout};
}

Record record(std::string value = "v") {
  return {.topic = "psim.test.v1", .key = "k", .value = std::move(value), .headers = {{"h", "1"}}, .partition = {}};
}

TEST(Producer, RejectsInvalidConfiguration) {
  asio::io_context io;
  auto options = nowhere();
  options.client.properties["no.such.property"] = "1";
  const auto unknown = Producer::create(io.get_executor(), options);
  ASSERT_FALSE(unknown.has_value());
  EXPECT_EQ(unknown.error().code(), ErrorCode::kCommonInvalidArgument);

  options = nowhere();
  options.client.properties["acks"] = "1";
  const auto weakened = Producer::create(io.get_executor(), options);
  ASSERT_FALSE(weakened.has_value());
  EXPECT_NE(weakened.error().message().find("acks is fixed"), std::string::npos);

  options = nowhere();
  options.client.bootstrap.clear();
  EXPECT_FALSE(Producer::create(io.get_executor(), options).has_value());
}

asio::awaitable<Result<Delivery>> send_nowhere(KafkaMetrics* metrics) {
  auto producer = Producer::create(co_await asio::this_coro::executor, nowhere(), {.metrics = metrics});
  EXPECT_TRUE(producer.has_value());
  co_return co_await (*producer)->send(record());
}

TEST(Producer, ReportsAFailedDeliveryToTheSender) {
  prometheus::Registry registry;
  KafkaMetrics metrics(registry);
  const auto result = run(send_nowhere(&metrics));
  ASSERT_FALSE(result.has_value());
  EXPECT_TRUE(result.error().retryable()) << result.error().message();
  EXPECT_EQ(metrics.delivery_errors("psim.test.v1").Value(), 1.0);
}

TEST(Producer, FlushReportsTheFirstDeliveryError) {
  const auto result = run([]() -> asio::awaitable<Result<void>> {
    auto producer = Producer::create(co_await asio::this_coro::executor, nowhere());
    EXPECT_TRUE((co_await (*producer)->enqueue(record("a"))).has_value());
    EXPECT_TRUE((co_await (*producer)->enqueue(record("b"))).has_value());
    auto first = co_await (*producer)->flush(milliseconds(5000));
    EXPECT_FALSE(first.has_value());
    co_return co_await (*producer)->flush(milliseconds(100));  // the error was reported once
  }());
  EXPECT_TRUE(result.has_value());
}

TEST(Producer, FlushGivesUpAtItsTimeout) {
  const auto result = run([]() -> asio::awaitable<Result<void>> {
    auto producer = Producer::create(co_await asio::this_coro::executor, nowhere(milliseconds(10000)));
    EXPECT_TRUE((co_await (*producer)->enqueue(record())).has_value());
    co_return co_await (*producer)->flush(milliseconds(50));
  }());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::kCommonDeadlineExceeded);
}

TEST(Producer, FlushNowBlocksUntilTheOutcome) {
  asio::io_context io;
  auto producer = Producer::create(io.get_executor(), nowhere());
  ASSERT_TRUE(producer.has_value());
  EXPECT_TRUE(run((*producer)->enqueue(record())).has_value());
  EXPECT_FALSE((*producer)->flush_now(milliseconds(5000)).has_value());
  EXPECT_TRUE((*producer)->flush_now(milliseconds(100)).has_value());
  EXPECT_NE((*producer)->handle(), nullptr);
}

TEST(Producer, TransactionsNeedATransactionalIdAndAPool) {
  run([]() -> asio::awaitable<void> {
    auto plain = Producer::create(co_await asio::this_coro::executor, nowhere());
    EXPECT_FALSE((*plain)->begin_transaction().has_value());
    EXPECT_FALSE((co_await (*plain)->init_transactions(milliseconds(10))).has_value());
    EXPECT_FALSE((co_await (*plain)->commit_transaction(nullptr, {}, milliseconds(10))).has_value());
    EXPECT_FALSE((co_await (*plain)->abort_transaction(milliseconds(10))).has_value());
    EXPECT_FALSE((*plain)->commit_transaction_now(nullptr, {}, milliseconds(10)).has_value());
    EXPECT_FALSE((*plain)->abort_transaction_now(milliseconds(10)).has_value());
    EXPECT_FALSE((*plain)->in_transaction());
    EXPECT_FALSE((*plain)->fatal());

    auto options = nowhere();
    options.transactional_id = "psim-test-0";
    auto without_pool = Producer::create(co_await asio::this_coro::executor, options);
    EXPECT_FALSE((co_await (*without_pool)->init_transactions(milliseconds(10))).has_value());
  }());
}

asio::awaitable<Result<void>> init_without_broker(async::BlockingPool* pool) {
  auto options = nowhere();
  options.transactional_id = "psim-test-1";
  auto producer = Producer::create(co_await asio::this_coro::executor, options, {.blocking = pool});
  co_return co_await (*producer)->init_transactions(milliseconds(300));
}

TEST(Producer, InitTransactionsTimesOutWithoutABroker) {
  async::BlockingPool pool(1, 4);
  const auto result = run(init_without_broker(&pool));
  ASSERT_FALSE(result.has_value());
  EXPECT_TRUE(result.error().retryable()) << result.error().message();
}

ConsumerOptions consumer_options() {
  return {.client = {.bootstrap = kNowhere, .client_id = "psim-test", .properties = {}},
          .group_id = "psim-test",
          .topics = {"psim.test.v1"},
          .read_committed = true,
          .group_protocol = "classic",
          .auto_offset_reset = "earliest"};
}

TEST(Consumer, RejectsInvalidConfiguration) {
  asio::io_context io;
  auto options = consumer_options();
  options.group_id.clear();
  EXPECT_FALSE(Consumer::create(io.get_executor(), options).has_value());

  options = consumer_options();
  options.client.properties["enable.auto.commit"] = "true";
  const auto auto_commit = Consumer::create(io.get_executor(), options);
  ASSERT_FALSE(auto_commit.has_value());
  EXPECT_EQ(auto_commit.error().code(), ErrorCode::kCommonInvalidArgument);
}

asio::awaitable<void> poll_without_broker(async::BlockingPool* pool) {
  auto consumer = Consumer::create(co_await asio::this_coro::executor, consumer_options(), {.blocking = pool});
  EXPECT_TRUE(consumer.has_value());
  auto batch = co_await (*consumer)->poll(10, milliseconds(200));
  EXPECT_TRUE(batch.has_value());
  EXPECT_TRUE(batch->empty());
  EXPECT_TRUE((*consumer)->assignment().empty());
  EXPECT_EQ((*consumer)->group(), "psim-test");
  EXPECT_TRUE((co_await (*consumer)->commit({})).has_value());
  EXPECT_TRUE((*consumer)->commit_now({}).has_value());
  EXPECT_TRUE((*consumer)->seek({}).has_value());
  EXPECT_NE((*consumer)->handle(), nullptr);
  EXPECT_NE((*consumer)->group_metadata(), nullptr);
  co_await (*consumer)->close();
  co_await (*consumer)->close();  // idempotent
}

TEST(Consumer, PollsNothingWithoutABrokerAndCloses) {
  async::BlockingPool pool(1, 4);
  run(poll_without_broker(&pool));
}

TEST(Consumer, CommitAndCommittedNeedAPool) {
  run([]() -> asio::awaitable<void> {
    auto consumer = Consumer::create(co_await asio::this_coro::executor, consumer_options());
    const Offsets offsets{{{.topic = "psim.test.v1", .partition = 0}, 1}};
    EXPECT_FALSE((co_await (*consumer)->commit(offsets)).has_value());
    EXPECT_FALSE(
        (co_await (*consumer)->committed({{.topic = "psim.test.v1", .partition = 0}}, milliseconds(10))).has_value());
  }());
}

}  // namespace
}  // namespace psim::platform::kafka
