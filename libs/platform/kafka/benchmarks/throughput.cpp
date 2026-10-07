// B-01: producer and consumer throughput of the Kafka layer against a broker (`task bench:kafka`).
//   psim_kafka_throughput <bootstrap> [messages] [value-bytes] [min-rate]
// Creates a topic of 12 partitions, produces the messages with the idempotent producer, reads them
// with a group consumer, prints the rates and fails when either is below min-rate (messages/s).

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <librdkafka/rdkafka.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <print>
#include <random>
#include <span>
#include <string>
#include <tuple>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"

namespace asio = boost::asio;
namespace kafka = psim::platform::kafka;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kPartitions = 12;

struct Rates {
  double produce = 0;
  double consume = 0;
};

// Topic administration through the C API: the layer itself never creates topics (the installer does).
bool admin(const std::string& bootstrap, const std::string& topic, bool create) {
  std::array<char, 512> error{};
  rd_kafka_conf_t* conf = rd_kafka_conf_new();
  rd_kafka_conf_set(conf, "bootstrap.servers", bootstrap.c_str(), error.data(), error.size());
  rd_kafka_t* handle = rd_kafka_new(RD_KAFKA_PRODUCER, conf, error.data(), error.size());
  if (handle == nullptr) {
    std::println(stderr, "admin: {}", error.data());
    return false;
  }
  rd_kafka_queue_t* queue = rd_kafka_queue_new(handle);
  if (create) {
    rd_kafka_NewTopic_t* spec = rd_kafka_NewTopic_new(topic.c_str(), kPartitions, -1, error.data(), error.size());
    rd_kafka_CreateTopics(handle, &spec, 1, nullptr, queue);
    rd_kafka_NewTopic_destroy(spec);
  } else {
    rd_kafka_DeleteTopic_t* spec = rd_kafka_DeleteTopic_new(topic.c_str());
    rd_kafka_DeleteTopics(handle, &spec, 1, nullptr, queue);
    rd_kafka_DeleteTopic_destroy(spec);
  }
  bool ok = false;
  if (rd_kafka_event_t* event = rd_kafka_queue_poll(queue, 30000); event != nullptr) {
    ok = rd_kafka_event_error(event) == RD_KAFKA_RESP_ERR_NO_ERROR;
    rd_kafka_event_destroy(event);
  }
  rd_kafka_queue_destroy(queue);
  rd_kafka_destroy(handle);
  return ok;
}

asio::awaitable<Rates> measure(std::string bootstrap, std::string topic, long messages, std::size_t size,
                               psim::platform::async::BlockingPool* pool) {
  const auto executor = co_await asio::this_coro::executor;
  const kafka::ClientOptions client{.bootstrap = bootstrap, .client_id = "psim-kafka-throughput"};
  Rates rates;
  {
    auto producer = kafka::Producer::create(executor, {.client = client});
    const std::string value(size, 'x');
    const auto start = Clock::now();
    for (long i = 0; i < messages; ++i) {
      if (auto queued =
              co_await (*producer)->enqueue({.topic = topic, .key = std::to_string(i % 1000), .value = value});
          !queued) {
        std::println(stderr, "produce: {}", queued.error().message());
        co_return rates;
      }
    }
    if (auto flushed = co_await (*producer)->flush(std::chrono::minutes(2)); !flushed) {
      std::println(stderr, "flush: {}", flushed.error().message());
      co_return rates;
    }
    rates.produce = static_cast<double>(messages) / std::chrono::duration<double>(Clock::now() - start).count();
  }
  auto consumer = kafka::Consumer::create(executor,
                                          {.client = client,
                                           .group_id = topic + "-bench",
                                           .topics = {topic},
                                           .read_committed = false,
                                           .group_protocol = "consumer",
                                           .auto_offset_reset = "earliest"},
                                          {.blocking = pool});
  long received = 0;
  Clock::time_point first{};
  const auto deadline = Clock::now() + std::chrono::minutes(2);
  while (received < messages && Clock::now() < deadline) {
    auto batch = co_await (*consumer)->poll(10000, std::chrono::milliseconds(500));
    if (!batch) {
      break;
    }
    if (received == 0 && !batch->empty()) {
      first = Clock::now();  // from the first message: the group join is not throughput
    }
    received += static_cast<long>(batch->size());
  }
  if (received >= messages) {
    rates.consume = static_cast<double>(received) / std::chrono::duration<double>(Clock::now() - first).count();
  }
  co_await (*consumer)->close();
  co_return rates;
}

int run(std::span<char*> args) {
  if (args.size() < 2) {
    std::println(stderr, "usage: psim_kafka_throughput <bootstrap> [messages] [value-bytes] [min-rate]");
    return 2;
  }
  const std::string bootstrap = args[1];
  const long messages = args.size() > 2 ? std::stol(args[2]) : 2'000'000;
  const std::size_t size = args.size() > 3 ? std::stoul(args[3]) : 512;
  const double min_rate = args.size() > 4 ? std::stod(args[4]) : 200'000;
  const std::string topic = "psim.bench.throughput." + std::to_string(std::random_device{}() % 1000000) + ".v1";
  if (!admin(bootstrap, topic, true)) {
    std::println(stderr, "cannot create the topic {}", topic);
    return 1;
  }
  asio::io_context io;
  psim::platform::async::BlockingPool pool(1, 4);
  auto future = asio::co_spawn(io, measure(bootstrap, topic, messages, size, &pool), asio::use_future);
  io.run();
  const auto rates = future.get();
  if (!admin(bootstrap, topic, false)) {
    std::println(stderr, "cannot delete the topic {}", topic);
  }
  std::println("B-01: {} messages of {} bytes: produce {:.0f} msg/s, consume {:.0f} msg/s (minimum {:.0f})", messages,
               size, rates.produce, rates.consume, min_rate);
  return rates.produce >= min_rate && rates.consume >= min_rate ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(std::span(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception& e) {
    std::ignore = std::fputs(e.what(), stderr);
    return 1;
  } catch (...) {
    return 1;
  }
}
