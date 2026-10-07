// Kafka layer against the broker and the Schema Registry of the local environment (TS-03).

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>
#include <opentelemetry/exporters/memory/in_memory_span_data.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/trace/tracer.h>
#include <prometheus/registry.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "component_support.hpp"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/clock.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/metrics.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "support.hpp"

namespace psim::platform::kafka::testing {
namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

ConsumerOptions consumer(const std::string& topic, std::string group, bool read_committed = true) {
  return {.client = client(),
          .group_id = std::move(group),
          .topics = {topic},
          .read_committed = read_committed,
          .group_protocol = "consumer",
          .auto_offset_reset = "earliest"};
}

asio::awaitable<void> sleep(milliseconds duration) {
  asio::steady_timer timer(co_await asio::this_coro::executor, duration);
  co_await timer.async_wait(asio::use_awaitable);
}

std::string stage_name() {
  return "test" + std::to_string(std::random_device{}() % 1000000);
}

// --- Delivery, headers, trace context --------------------------------------------------------------

struct RoundTrip {
  std::string topic;
  KafkaMetrics* metrics;
  async::BlockingPool* pool;
  opentelemetry::trace::Tracer* tracer;
};

asio::awaitable<void> round_trip(RoundTrip test) {
  const auto executor = co_await asio::this_coro::executor;
  auto producer = Producer::create(executor, {.client = client()}, {.metrics = test.metrics});
  const auto produce_span = test.tracer->StartSpan("produce");
  Record record{.topic = test.topic, .key = "device-1", .value = "payload", .headers = {{"psim-ts-test", "1"}}};
  inject(*produce_span, record.headers);
  const auto delivered = co_await (*producer)->send(record);
  produce_span->End();
  EXPECT_TRUE(delivered.has_value()) << delivered.error().message();

  auto input = Consumer::create(executor, consumer(test.topic, unique("group")),
                                {.blocking = test.pool, .metrics = test.metrics});
  std::vector<Message> messages;
  for (int i = 0; i < 100 && messages.empty(); ++i) {
    auto batch = co_await (*input)->poll(10, milliseconds(200));
    messages = std::move(batch.value());
  }
  EXPECT_EQ(messages.size(), 1U);
  if (!messages.empty()) {
    const auto& message = messages.front();
    EXPECT_EQ(message.key(), "device-1");
    EXPECT_EQ(message.value(), "payload");
    EXPECT_EQ(message.partition(), delivered->partition);
    EXPECT_EQ(message.offset(), delivered->offset);
    EXPECT_EQ(message.header("psim-ts-test"), "1");
    EXPECT_TRUE(message.header("traceparent").has_value());
    EXPECT_EQ(message.headers().size(), 2U);
    EXPECT_TRUE(message.timestamp().has_value());
    EXPECT_TRUE(extract(message).IsRemote());
    start_consumer_span(*test.tracer, "consume", message)->End();
    EXPECT_TRUE((co_await (*input)->commit({{message.topic_partition(), message.offset() + 1}})).has_value());
    const auto committed = co_await (*input)->committed({message.topic_partition()}, milliseconds(5000));
    EXPECT_EQ(committed.value().at(message.topic_partition()), message.offset() + 1);
  }
  co_await (*input)->close();
}

TEST(KafkaComponent, DeliversKeysValuesHeadersAndTheTraceContext) {
  Topics topics;
  prometheus::Registry registry;
  KafkaMetrics metrics(registry);
  async::BlockingPool pool(1, 4);
  auto exporter = std::make_unique<opentelemetry::exporter::memory::InMemorySpanExporter>();
  const auto spans = exporter->GetData();
  const auto provider = std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
      std::make_unique<opentelemetry::sdk::trace::SimpleSpanProcessor>(std::move(exporter)));
  const auto tracer = provider->GetTracer("test");
  const auto topic = topics.create("roundtrip", 3);

  run(round_trip({.topic = topic, .metrics = &metrics, .pool = &pool, .tracer = tracer.get()}));

  const auto finished = spans->GetSpans();
  ASSERT_EQ(finished.size(), 2U);
  EXPECT_EQ(finished[1]->GetParentSpanId(), finished[0]->GetSpanId());
  EXPECT_EQ(finished[1]->GetTraceId(), finished[0]->GetTraceId());
  EXPECT_EQ(metrics.produced_messages(topic).Value(), 1.0);
  EXPECT_EQ(metrics.consumed_messages(topic).Value(), 1.0);
}

// --- Broker faults and transactions ---------------------------------------------------------------

constexpr int kFaultMessages = 20000;

asio::awaitable<void> produce_through_a_cut(std::string topic) {
  auto producer = Producer::create(co_await asio::this_coro::executor, {.client = client(environment().kafka_fault)});
  for (int i = 0; i < kFaultMessages; ++i) {
    if (i == kFaultMessages / 2) {
      toxiproxy_enable("kafka-1", false);  // in-flight batches fail and are retried
      co_await sleep(milliseconds(3000));
      toxiproxy_enable("kafka-1", true);
    }
    EXPECT_TRUE((co_await (*producer)->enqueue({.topic = topic, .key = std::to_string(i), .value = std::to_string(i)}))
                    .has_value());
  }
  const auto flushed = co_await (*producer)->flush(milliseconds(120000));
  EXPECT_TRUE(flushed.has_value()) << flushed.error().message();
}

TEST(KafkaComponent, IdempotentProducerLosesAndDuplicatesNothingWhenTheConnectionIsCut) {
  Topics topics;
  const auto topic = topics.create("fault", 3);
  run(produce_through_a_cut(topic));
  const auto counts = histogram(read_all(topic, kFaultMessages, seconds(60)));
  EXPECT_EQ(counts.size(), static_cast<std::size_t>(kFaultMessages));
  for (const auto& [value, count] : counts) {
    ASSERT_EQ(count, 1) << "value " << value;
  }
}

asio::awaitable<void> abort_then_commit(std::string topic, async::BlockingPool* pool) {
  auto producer =
      Producer::create(co_await asio::this_coro::executor,
                       {.client = client(), .transactional_id = unique("txn-producer")}, {.blocking = pool});
  EXPECT_TRUE((co_await (*producer)->init_transactions(milliseconds(30000))).has_value());
  EXPECT_TRUE((*producer)->begin_transaction().has_value());
  for (int i = 0; i < 10; ++i) {
    std::ignore = co_await (*producer)->enqueue({.topic = topic, .key = "a", .value = "aborted"});
  }
  EXPECT_TRUE((co_await (*producer)->abort_transaction(milliseconds(30000))).has_value());
  EXPECT_TRUE((*producer)->begin_transaction().has_value());
  for (int i = 0; i < 5; ++i) {
    std::ignore = co_await (*producer)->enqueue({.topic = topic, .key = "c", .value = "committed"});
  }
  EXPECT_TRUE((co_await (*producer)->commit_transaction(nullptr, {}, milliseconds(30000))).has_value());
  EXPECT_FALSE((*producer)->in_transaction());
}

TEST(KafkaComponent, ReadCommittedSkipsAbortedTransactions) {
  Topics topics;
  const auto topic = topics.create("txn", 1);
  async::BlockingPool pool(1, 4);
  run(abort_then_commit(topic, &pool));
  const auto committed = histogram(read_all(topic, 6, seconds(10)));
  EXPECT_EQ(committed.size(), 1U);
  EXPECT_EQ(committed.at("committed"), 5);
}

// --- Processor ------------------------------------------------------------------------------------

/// Runs a processor on its own thread until result().
class Stage {
 public:
  Stage(ConsumerOptions input, ProcessorOptions options, KafkaMetrics* metrics, Handler* handler)
      : input_(std::move(input)), options_(std::move(options)), metrics_(metrics), handler_(handler), thread_([this] {
          run_stage();
        }) {}

  Stage(const Stage&) = delete;
  Stage& operator=(const Stage&) = delete;
  Stage(Stage&&) = delete;
  Stage& operator=(Stage&&) = delete;

  ~Stage() { std::ignore = result(); }

  /// Stop the processor (it commits) and wait for its outcome.
  Result<void> result() {
    stop_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
    return result_;
  }

 private:
  asio::awaitable<Result<void>> process(Processor* processor) {
    auto result = co_await processor->run(handler_);
    finished_ = true;
    co_return result;
  }

  asio::awaitable<void> watch(Processor* processor) {
    while (!stop_ && !finished_) {
      co_await sleep(milliseconds(50));
    }
    processor->stop();
  }

  asio::awaitable<Result<void>> stage(async::BlockingPool* pool, async::Clock* clock) {
    const auto executor = co_await asio::this_coro::executor;
    const Dependencies deps{.blocking = pool, .metrics = metrics_};
    auto input = Consumer::create(executor, input_, deps);
    ProducerOptions output{.client = client()};
    if (options_.semantics == Semantics::kExactlyOnce) {
      output.transactional_id = input_.group_id + "-tx";
    }
    auto producer = Producer::create(executor, output, deps);
    Processor processor(input->get(), producer->get(), clock, options_, deps);
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operator&&
    auto result = co_await (process(&processor) && watch(&processor));
    co_await (*input)->close();
    co_return result;
  }

  void run_stage() {
    asio::io_context io;
    async::BlockingPool pool(2, 16);
    async::SystemClock clock;
    auto future = asio::co_spawn(io, stage(&pool, &clock), asio::use_future);
    io.run();
    result_ = future.get();
  }

  ConsumerOptions input_;
  ProcessorOptions options_;
  KafkaMetrics* metrics_;
  Handler* handler_;
  std::atomic<bool> stop_{false};
  bool finished_ = false;
  Result<void> result_;
  std::thread thread_;
};

bool wait_until(const std::function<bool()>& condition, milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(milliseconds(50));
  }
  return condition();
}

/// Copies every message to `output` after an optional delay, counting them.
class Copy final : public Handler {
 public:
  explicit Copy(std::string output, milliseconds delay = milliseconds(0)) : output_(std::move(output)), delay_(delay) {}

  asio::awaitable<Result<void>> handle(const Message* message, Output* out) override {
    ++count_;
    if (delay_.count() > 0) {
      co_await sleep(delay_);
    }
    co_return co_await out->send(
        {.topic = output_, .key = std::string(message->key()), .value = std::string(message->value())});
  }

  [[nodiscard]] int count() const { return count_; }

 private:
  std::atomic<int> count_{0};
  std::string output_;
  milliseconds delay_;
};

/// "5" fails twice with a transient error, "7" is invalid, "9" throws; the rest is copied.
class Faulty final : public Handler {
 public:
  explicit Faulty(std::string output) : output_(std::move(output)) {}

  asio::awaitable<Result<void>> handle(const Message* message, Output* out) override {
    const std::string value(message->value());
    if (value == "5" && failures_of_5_++ < 2) {
      co_return fail(ErrorCode::kCommonUnavailable, "storage is down");
    }
    ++handled_;
    if (value == "7") {
      co_return fail(ErrorCode::kProcessingPayloadInvalid, "bad payload");
    }
    if (value == "9") {
      throw std::logic_error("invariant violated");
    }
    co_return co_await out->send({.topic = output_, .key = std::string(message->key()), .value = value});
  }

  [[nodiscard]] int handled() const { return handled_; }

 private:
  std::atomic<int> handled_{0};
  std::string output_;
  int failures_of_5_ = 0;
};

/// Always a program error.
class Broken final : public Handler {
 public:
  asio::awaitable<Result<void>> handle(const Message* /*message*/, Output* /*out*/) override {
    ++calls_;
    throw std::runtime_error("bug");
    co_return Result<void>{};
  }

  [[nodiscard]] int calls() const { return calls_; }

 private:
  std::atomic<int> calls_{0};
};

TEST(KafkaComponent, ProcessorRetriesTransientErrorsAndSendsOthersToTheDeadLetterTopic) {
  Topics topics;
  const auto input = topics.create("alo-in", 2);
  const auto output = topics.create("alo-out", 2);
  const auto stage = stage_name();
  const auto dead_letters = topics.create_named("psim.dlq." + stage + ".v1", 1);
  produce_numbers(input, 100);

  prometheus::Registry registry;
  KafkaMetrics metrics(registry);
  Faulty handler(output);
  const auto group = unique("alo-group");
  {
    Stage processor(consumer(input, group),
                    {.stage = stage,
                     .semantics = Semantics::kAtLeastOnce,
                     .commit_interval = milliseconds(200),
                     .retry = {.initial = milliseconds(50), .max = milliseconds(200), .multiplier = 2.0}},
                    &metrics, &handler);
    ASSERT_TRUE(wait_until([&] { return handler.handled() >= 100; }, milliseconds(60000))) << handler.handled();
    EXPECT_TRUE(processor.result().has_value());
  }
  const auto outputs = histogram(read_all(output, 98, seconds(20)));
  EXPECT_EQ(outputs.size(), 98U);
  EXPECT_FALSE(outputs.contains("7"));
  EXPECT_FALSE(outputs.contains("9"));
  EXPECT_EQ(histogram(read_all(dead_letters, 2, seconds(20))).size(), 2U);
  EXPECT_EQ(metrics.retries(stage).Value(), 2.0);
  EXPECT_EQ(metrics.dead_letters(stage, "PROCESSING_PAYLOAD_INVALID").Value(), 1.0);
  EXPECT_EQ(metrics.dead_letters(stage, "COMMON_INTERNAL").Value(), 1.0);

  // Offsets were committed: the group has nothing left to read.
  Copy again(output);
  {
    const Stage second(consumer(input, group), {.stage = stage, .commit_interval = milliseconds(200)}, nullptr, &again);
    std::this_thread::sleep_for(seconds(5));
  }
  EXPECT_EQ(again.count(), 0);
}

TEST(KafkaComponent, CircuitBreakerStopsAPartitionAfterRepeatedProgramErrors) {
  Topics topics;
  const auto input = topics.create("breaker-in", 1);
  const auto stage = stage_name();
  const auto dead_letters = topics.create_named("psim.dlq." + stage + ".v1", 1);
  produce_numbers(input, 10);
  prometheus::Registry registry;
  KafkaMetrics metrics(registry);
  Broken handler;
  {
    Stage processor(consumer(input, unique("breaker")),
                    {.stage = stage, .commit_interval = milliseconds(100), .circuit_breaker = 3}, &metrics, &handler);
    ASSERT_TRUE(wait_until([&] { return metrics.stopped_partitions(stage).Value() == 1.0; }, milliseconds(30000)));
    std::this_thread::sleep_for(milliseconds(1000));
    EXPECT_TRUE(processor.result().has_value());
  }
  EXPECT_EQ(handler.calls(), 3);
  EXPECT_EQ(histogram(read_all(dead_letters, 3, seconds(20))).size(), 3U);
}

TEST(KafkaComponent, RebalanceMovesPartitionsWithoutLoss) {
  Topics topics;
  const auto input = topics.create("rebalance-in", 6);
  const auto output = topics.create("rebalance-out", 6);
  constexpr int kMessages = 3000;
  produce_numbers(input, kMessages);
  const auto group = unique("rebalance");
  Copy first_handler(output, milliseconds(5));
  Copy second_handler(output, milliseconds(5));
  const ProcessorOptions options{.stage = "rebalance", .max_batch = 50, .commit_interval = milliseconds(100)};
  {
    Stage first(consumer(input, group), options, nullptr, &first_handler);
    ASSERT_TRUE(wait_until([&] { return first_handler.count() >= 300; }, milliseconds(30000)));
    Stage second(consumer(input, group), options, nullptr, &second_handler);
    ASSERT_TRUE(
        wait_until([&] { return first_handler.count() + second_handler.count() >= kMessages; }, milliseconds(120000)));
    std::this_thread::sleep_for(milliseconds(500));
    EXPECT_TRUE(second.result().has_value());
    EXPECT_TRUE(first.result().has_value());
  }
  EXPECT_GT(second_handler.count(), 0);
  const auto outputs = histogram(read_all(output, kMessages, seconds(30)));
  EXPECT_EQ(outputs.size(), static_cast<std::size_t>(kMessages));  // duplicates allowed (ALO), losses not
}

// --- Schema Registry ------------------------------------------------------------------------------

asio::awaitable<void> serde_round_trip(SchemaRegistry* registry, std::string topic) {
  auto serde = co_await ProtobufSerde<ingest::v1::RawEventRecord>::create(registry, "psim.ingest.raw.v1");
  EXPECT_TRUE(serde.has_value()) << serde.error().message();
  if (!serde) {
    co_return;
  }
  const auto schema = co_await registry->schema(serde->writer_id());
  EXPECT_NE(schema.value().find("RawEventRecord"), std::string::npos);

  ingest::v1::RawEventRecord record;
  record.mutable_envelope()->set_message_id("0190f1a2-0000-7000-8000-000000000001");
  record.mutable_raw_event()->set_raw_code("DOOR_FORCED");
  auto producer = Producer::create(co_await asio::this_coro::executor, {.client = client()});
  EXPECT_TRUE(
      (co_await (*producer)->send({.topic = topic, .key = "source", .value = serde->serialize(record)})).has_value());
}

TEST(KafkaComponent, SerdeUsesTheSchemaRegisteredForTheTopic) {
  Topics topics;
  const auto topic = topics.create("serde", 1);
  auto registry = SchemaRegistry::create({.url = environment().registry, .timeout = milliseconds(5000)});
  ASSERT_TRUE(registry.has_value());
  run(serde_round_trip(&*registry, topic));
  const auto values = read_all(topic, 1, seconds(20));
  ASSERT_EQ(values.size(), 1U);
  const ProtobufSerde<ingest::v1::RawEventRecord> reader(0);
  const auto decoded = reader.deserialize(values.front().second);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->raw_event().raw_code(), "DOOR_FORCED");
  const auto missing = run(registry->latest_id("psim.no.such.topic.v1-value"));
  EXPECT_EQ(missing.error().code(), ErrorCode::kCommonNotFound);
}

}  // namespace
}  // namespace psim::platform::kafka::testing
