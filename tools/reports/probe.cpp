#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/json_util.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include "psim/incident/v1/events.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/response/v1/events.pb.h"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Json = nlohmann::json;

template <class T>
T take(p::Result<T> r) {
  if (!r) {
    throw std::runtime_error(r.error().message());
  }
  return std::move(*r);
}

void take(p::Result<void> r) {
  if (!r) {
    throw std::runtime_error(r.error().message());
  }
}

asio::awaitable<int> emit(Json input) {
  p::async::BlockingPool blocking(4, 64);
  auto executor = co_await asio::this_coro::executor;
  auto producer = take(p::kafka::Producer::create(
      executor, {.client = {.bootstrap = "kafka-1:19092", .client_id = "report-probe", .allow_plaintext = true}},
      {.blocking = &blocking}));
  auto registry = take(p::kafka::SchemaRegistry::create(
      {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
  const std::string topic = input.value("topic", "psim.incidents.events.v1");
  const auto schema = take(co_await registry.latest_id(topic + "-value"));
  for (const auto& item : input.at("records")) {
    std::unique_ptr<google::protobuf::Message> message;
    if (topic == "psim.incidents.events.v1") {
      message = std::make_unique<psim::incident::v1::IncidentEventRecord>();
    } else {
      message = std::make_unique<psim::response::v1::ResponseEventRecord>();
    }
    if (!google::protobuf::util::JsonStringToMessage(item.at("record").dump(), message.get()).ok()) {
      throw std::runtime_error("invalid fixture record");
    }
    std::string value;
    p::kafka::wire::write_header(value, schema, p::kafka::wire::message_indexes(*message->GetDescriptor()));
    value += message->SerializeAsString();
    if (item.value("malformed", false)) {
      value = "malformed";
    }
    for (int repeat = 0; repeat < input.value("repeat", 1); ++repeat) {
      take(co_await producer->send({.topic = topic, .key = item.at("key"), .value = value}));
    }
  }
  take(co_await producer->flush(std::chrono::seconds(30)));
  co_return 0;
}
}  // namespace

int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  if (argc != 2) {
    return 2;
  }
  try {
    std::ifstream file(args[1]);
    auto input = Json::parse(file);
    asio::io_context context;
    auto result = asio::co_spawn(context, emit(input), asio::use_future);
    context.run();
    return result.get();
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
