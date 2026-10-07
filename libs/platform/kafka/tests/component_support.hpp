#pragma once

// Component tests against the local environment (TS-03): endpoints come from PSIM_TEST_* variables
// set by `task test:component`; topics are created per test with unique names.

#include <boost/asio/awaitable.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <gtest/gtest.h>
#include <librdkafka/rdkafka.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "support.hpp"

namespace psim::platform::kafka::testing {

struct Environment {
  std::string kafka;        // brokers on the compose network
  std::string kafka_fault;  // the same broker through Toxiproxy (proxy "kafka-1")
  std::string toxiproxy;    // host:port of the Toxiproxy API
  std::string registry;     // Confluent-compatible API of the Schema Registry
};

inline std::string required(const char* name) {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): read before threads start
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string(name) + " is not set: component tests run by `task test:component`");
  }
  return value;
}

inline const Environment& environment() {
  static const Environment kEnvironment{.kafka = required("PSIM_TEST_KAFKA"),
                                        .kafka_fault = required("PSIM_TEST_KAFKA_FAULT"),
                                        .toxiproxy = required("PSIM_TEST_TOXIPROXY"),
                                        .registry = required("PSIM_TEST_REGISTRY")};
  return kEnvironment;
}

inline std::string unique(std::string_view what) {
  static std::mt19937_64 random{std::random_device{}()};
  return "psim.test." + std::string(what) + "." + std::to_string(random() % 1'000'000'000) + ".v1";
}

inline ClientOptions client(std::string bootstrap = environment().kafka) {
  return {.bootstrap = std::move(bootstrap), .client_id = "psim-component-test", .properties = {}};
}

/// Topics created for one test and deleted with it.
class Topics {
 public:
  Topics() {
    std::array<char, 512> error{};
    rd_kafka_conf_t* conf = rd_kafka_conf_new();
    rd_kafka_conf_set(conf, "bootstrap.servers", environment().kafka.c_str(), error.data(), error.size());
    handle_.reset(rd_kafka_new(RD_KAFKA_PRODUCER, conf, error.data(), error.size()));
    if (!handle_) {
      throw std::runtime_error(error.data());
    }
  }

  Topics(const Topics&) = delete;
  Topics& operator=(const Topics&) = delete;
  Topics(Topics&&) = delete;
  Topics& operator=(Topics&&) = delete;

  ~Topics() {
    try {
      remove(created_);
    } catch (...) {  // NOLINT(bugprone-empty-catch): leftover test topics are harmless
    }
  }

  std::string create(std::string_view what, int partitions) { return create_named(unique(what), partitions); }

  std::string create_named(std::string name, int partitions) {
    std::array<char, 512> error{};
    rd_kafka_NewTopic_t* topic = rd_kafka_NewTopic_new(name.c_str(), partitions, 1, error.data(), error.size());
    rd_kafka_queue_t* queue = rd_kafka_queue_new(handle_.get());
    rd_kafka_CreateTopics(handle_.get(), &topic, 1, nullptr, queue);
    rd_kafka_event_t* event = rd_kafka_queue_poll(queue, 30000);
    std::string failure;
    if (event == nullptr) {
      failure = "timeout";
    } else {
      std::size_t count = 0;
      const auto* results = rd_kafka_CreateTopics_result_topics(rd_kafka_event_CreateTopics_result(event), &count);
      for (std::size_t i = 0; i < count; ++i) {
        const auto* result = results[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): C array
        if (rd_kafka_topic_result_error(result) != RD_KAFKA_RESP_ERR_NO_ERROR) {
          failure = rd_kafka_topic_result_error_string(result);
        }
      }
      rd_kafka_event_destroy(event);
    }
    rd_kafka_queue_destroy(queue);
    rd_kafka_NewTopic_destroy(topic);
    if (!failure.empty()) {
      throw std::runtime_error("create topic " + name + ": " + failure);
    }
    created_.push_back(name);
    return name;
  }

 private:
  struct Deleter {
    void operator()(rd_kafka_t* handle) const noexcept { rd_kafka_destroy(handle); }
  };

  void remove(const std::vector<std::string>& names) {
    if (names.empty()) {
      return;
    }
    std::vector<rd_kafka_DeleteTopic_t*> topics;
    topics.reserve(names.size());
    for (const auto& name : names) {
      topics.push_back(rd_kafka_DeleteTopic_new(name.c_str()));
    }
    rd_kafka_queue_t* queue = rd_kafka_queue_new(handle_.get());
    rd_kafka_DeleteTopics(handle_.get(), topics.data(), topics.size(), nullptr, queue);
    if (rd_kafka_event_t* event = rd_kafka_queue_poll(queue, 30000); event != nullptr) {
      rd_kafka_event_destroy(event);
    }
    rd_kafka_queue_destroy(queue);
    rd_kafka_DeleteTopic_destroy_array(topics.data(), topics.size());
  }

  std::unique_ptr<rd_kafka_t, Deleter> handle_;
  std::vector<std::string> created_;
};

using Values = std::vector<std::pair<std::string, std::string>>;

inline asio::awaitable<Values> read_values(std::string topic, std::size_t count, std::chrono::milliseconds timeout,
                                           bool read_committed, async::BlockingPool* pool) {
  auto consumer = Consumer::create(co_await asio::this_coro::executor,
                                   {.client = client(),
                                    .group_id = unique("reader"),
                                    .topics = {topic},
                                    .read_committed = read_committed,
                                    .group_protocol = "consumer",
                                    .auto_offset_reset = "earliest"},
                                   {.blocking = pool});
  Values out;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (out.size() < count && std::chrono::steady_clock::now() < deadline) {
    auto batch = co_await (*consumer)->poll(1000, std::chrono::milliseconds(200));
    for (const auto& message : batch.value()) {
      out.emplace_back(std::string(message.key()), std::string(message.value()));
    }
  }
  co_await (*consumer)->close();
  co_return out;
}

/// Read `topic` from the beginning with a fresh group until `count` messages arrived or `timeout`.
inline Values read_all(const std::string& topic, std::size_t count, std::chrono::milliseconds timeout,
                       bool read_committed = true) {
  async::BlockingPool pool(1, 4);
  return run(read_values(topic, count, timeout, read_committed, &pool));
}

inline asio::awaitable<void> produce_values(std::string topic, int count) {
  auto producer = Producer::create(co_await asio::this_coro::executor, {.client = client()});
  for (int i = 0; i < count; ++i) {
    EXPECT_TRUE((co_await (*producer)->enqueue({.topic = topic, .key = std::to_string(i), .value = std::to_string(i)}))
                    .has_value());
  }
  EXPECT_TRUE((co_await (*producer)->flush(std::chrono::milliseconds(60000))).has_value());
}

/// Produce keys and values "0".."count-1" and wait for all deliveries.
inline void produce_numbers(const std::string& topic, int count) {
  run(produce_values(topic, count));
}

/// Enable or disable a Toxiproxy proxy: disabling cuts its connections.
inline void toxiproxy_enable(const std::string& proxy, bool enabled) {
  namespace http = boost::beast::http;
  const auto& api = environment().toxiproxy;
  const auto colon = api.rfind(':');
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket socket(io);
  asio::connect(socket, resolver.resolve(api.substr(0, colon), api.substr(colon + 1)));
  http::request<http::string_body> request(http::verb::post, "/proxies/" + proxy, 11);
  request.set(http::field::host, api);
  request.set(http::field::content_type, "application/json");
  request.body() = enabled ? R"({"enabled": true})" : R"({"enabled": false})";
  request.prepare_payload();
  http::write(socket, request);
  boost::beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  if (response.result_int() != 200) {
    throw std::runtime_error("toxiproxy " + proxy + ": HTTP " + std::to_string(response.result_int()));
  }
}

/// Values as a multiset count: every expected value once means no loss and no duplicates.
inline std::map<std::string, int> histogram(const std::vector<std::pair<std::string, std::string>>& messages) {
  std::map<std::string, int> counts;
  for (const auto& [key, value] : messages) {
    ++counts[value];
  }
  return counts;
}

}  // namespace psim::platform::kafka::testing
