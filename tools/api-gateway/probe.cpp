#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/config/v1/config.pb.h>
#include <psim/incident/v1/events.pb.h>
#include <psim/ingest/v1/state.pb.h>
#include <psim/processing/v1/event.pb.h>
#include <psim/response/v1/events.pb.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/redis/patterns.hpp"
#include "psim/platform/redis/pool.hpp"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Json = nlohmann::json;

template <typename T>
T take(p::Result<T> result) {
  if (!result) {
    throw std::runtime_error(result.error().message());
  }
  return std::move(*result);
}

asio::awaitable<int> run(Json input) {
  const auto executor = co_await asio::this_coro::executor;
  p::async::BlockingPool blocking(2, 16);
  auto producer = take(p::kafka::Producer::create(
      executor, {.client = {.bootstrap = "kafka-1:19092", .client_id = "api-probe", .allow_plaintext = true}},
      {.blocking = &blocking}));
  auto registry = take(p::kafka::SchemaRegistry::create(
      {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
  p::redis::PoolOptions options;
  options.connection.endpoint.host = "valkey";
  options.connection.credentials.password = "psim-dev-valkey";
  options.connection.tls.enabled = false;
  p::redis::Pool redis(options);
  for (const auto& lease : input.value("releases", Json::array())) {
    auto released = co_await p::redis::SessionRegistry(&redis, {"connector-gateway", lease.at("tenant")})
                        .release(lease.at("connector"), {lease.at("session"), "api-probe"});
    (void)take(std::move(released));
  }
  for (const auto& lease : input.value("leases", Json::array())) {
    auto claimed = co_await p::redis::SessionRegistry(&redis, {"connector-gateway", lease.at("tenant")})
                       .claim(lease.at("connector"), {lease.at("session"), "api-probe"},
                              std::chrono::seconds(lease.value("ttlSeconds", 60)));
    if (!take(std::move(claimed))) {
      throw std::runtime_error("lease claim failed");
    }
  }
  for (const auto& item : input.value("records", Json::array())) {
    const auto topic = item.at("topic").get<std::string>();
    std::unique_ptr<google::protobuf::Message> value;
    if (topic == "psim.catalog.v1") {
      value = std::make_unique<psim::catalog::v1::CatalogRecord>();
    } else if (topic == "psim.events.normalized.v1") {
      value = std::make_unique<psim::processing::v1::EventRecord>();
    } else if (topic == "psim.ingest.state.v1") {
      value = std::make_unique<psim::ingest::v1::GatewayStateRecord>();
    } else if (topic == "psim.config.v1") {
      value = std::make_unique<psim::config::v1::ConfigRecord>();
    } else if (topic == "psim.response.events.v1") {
      value = std::make_unique<psim::response::v1::ResponseEventRecord>();
    } else if (topic == "psim.incidents.events.v1") {
      value = std::make_unique<psim::incident::v1::IncidentEventRecord>();
    } else {
      throw std::runtime_error("unsupported fixture topic");
    }
    const auto schema = take(co_await registry.latest_id(topic + "-value"));
    auto body = item.at("value");
    if (input.value("stampProducedAt", false)) {
      body["envelope"]["producedAt"] =
          google::protobuf::util::TimeUtil::ToString(google::protobuf::util::TimeUtil::GetCurrentTime());
    }
    if (!google::protobuf::util::JsonStringToMessage(body.dump(), value.get()).ok()) {
      throw std::runtime_error("invalid fixture message");
    }
    std::string bytes;
    p::kafka::wire::write_header(bytes, schema, p::kafka::wire::message_indexes(*value->GetDescriptor()));
    value->AppendToString(&bytes);
    auto sent = co_await producer->send({.topic = topic, .key = item.at("key"), .value = std::move(bytes)});
    if (!sent) {
      throw std::runtime_error(sent.error().message());
    }
  }
  redis.close();
  producer.reset();
  co_return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("fixture path required");
    }
    std::ifstream file(std::span(argv, static_cast<std::size_t>(argc))[1]);
    const auto input = Json::parse(file);
    asio::io_context io;
    auto result = asio::co_spawn(io, run(input), asio::use_future);
    io.run();
    return result.get();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
