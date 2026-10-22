#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <psim/catalog/v1/catalog.pb.h>
#include <psim/incident/v1/events.pb.h>
#include <psim/response/v1/command.pb.h>
#include <psim/response/v1/events.pb.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "psim/connector/v1/connector.pb.h"
#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/connector/client.hpp"
#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace {
namespace p = psim::platform;
namespace asio = boost::asio;
using Json = nlohmann::json;
using Time = google::protobuf::util::TimeUtil;

template <typename T>
T take(p::Result<T> result) {
  if (!result) {
    throw std::runtime_error(result.error().message());
  }
  return std::move(*result);
}

void take(p::Result<void> result) {
  if (!result) {
    throw std::runtime_error(result.error().message());
  }
}

asio::awaitable<void> emit(Json input) {
  p::async::BlockingPool blocking(2, 16);
  auto executor = co_await asio::this_coro::executor;
  auto producer = take(p::kafka::Producer::create(
      executor, {.client = {.bootstrap = "kafka-1:19092", .client_id = "command-probe", .allow_plaintext = true}},
      {.blocking = &blocking}));
  if (input.contains("duplicate")) {
    auto consumer = take(p::kafka::Consumer::create(
        executor, {.client = {.bootstrap = "kafka-1:19092", .client_id = "command-duplicate", .allow_plaintext = true},
                   .group_id = p::stream::uuid7(),
                   .topics = {"psim.commands.v1"},
                   .group_protocol = "classic"}));
    bool found = false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!found && std::chrono::steady_clock::now() < until) {
      auto batch = take(co_await consumer->poll(1000, std::chrono::milliseconds(100)));
      for (const auto& message : batch) {
        auto parsed =
            p::kafka::ProtobufSerde<psim::response::v1::CommandDeliveryRecord>(1).deserialize(message.value());
        if (parsed && parsed->command_delivery().command_id() == input.at("duplicate").get<std::string>()) {
          for (int index = 0; index < 3; ++index) {
            take(co_await producer->send({.topic = "psim.commands.v1",
                                          .key = std::string(message.key()),
                                          .value = std::string(message.value())}));
          }
          found = true;
          break;
        }
      }
    }
    co_await consumer->close();
    if (!found) {
      throw std::runtime_error("delivery missing");
    }
  } else {
    auto registry = take(p::kafka::SchemaRegistry::create(
        {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}}));
    for (auto item : input.at("records")) {
      const auto topic = item.at("topic").get<std::string>();
      std::unique_ptr<google::protobuf::Message> record;
      if (topic == "psim.catalog.v1") {
        record = std::make_unique<psim::catalog::v1::CatalogRecord>();
      } else if (topic == "psim.incidents.events.v1") {
        record = std::make_unique<psim::incident::v1::IncidentEventRecord>();
      } else if (topic == "psim.commands.results.v1") {
        record = std::make_unique<psim::response::v1::CommandResultRecord>();
      } else if (topic == "psim.response.events.v1") {
        record = std::make_unique<psim::response::v1::ResponseEventRecord>();
      } else {
        throw std::runtime_error("unknown fixture topic");
      }
      auto& origin = item["value"]["envelope"];
      origin["messageId"] = origin.value("messageId", p::stream::uuid7());
      origin["schemaVersion"] = "1";
      origin["occurredAt"] = Time::ToString(Time::GetCurrentTime());
      origin["producedAt"] = origin["occurredAt"];
      if (!google::protobuf::util::JsonStringToMessage(item["value"].dump(), record.get()).ok()) {
        throw std::runtime_error("invalid fixture");
      }
      const auto schema = take(co_await registry.latest_id(topic + "-value"));
      std::string bytes;
      p::kafka::wire::write_header(bytes, schema, p::kafka::wire::message_indexes(*record->GetDescriptor()));
      record->AppendToString(&bytes);
      for (int index = 0; index < item.value("repeat", 1); ++index) {
        take(co_await producer->send({.topic = topic, .key = item.at("key").get<std::string>(), .value = bytes}));
      }
    }
  }
  take(co_await producer->flush(std::chrono::seconds(30)));
  producer.reset();
}

void fixture(const Json& input) {
  const std::string directory = input.at("directory");
  std::filesystem::create_directories(directory);
  const p::connector::GrpcProcess grpc;
  p::connector::Client client(
      {.endpoint = "connector-gateway:8080",
       .ca_file = ".cache/event-flow/ca.pem",
       .certificate_file = ".cache/event-flow/client.pem",
       .private_key_file = ".cache/event-flow/client.key",
       .buffer_directory = directory + "/sdk"},
      {.command = [directory](const psim::connector::v1::Command& command, const std::stop_token& stop) {
        std::ofstream output(directory + "/executions.jsonl", std::ios::app);
        output << Json{{"id", command.command_id()},
                       {"at", Time::ToString(Time::GetCurrentTime())},
                       {"capability", command.capability()}}
                      .dump()
               << '\n';
        output.close();
        if (command.capability() == "hold") {
          while (!stop.stop_requested() && !std::filesystem::exists(directory + "/release")) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        }
        psim::connector::v1::CommandResult result;
        result.set_command_id(command.command_id());
        result.set_success(command.capability() != "reject");
        if (!result.success()) {
          result.set_code("COMMAND_DEVICE_REJECTED");
        }
        return result;
      }});
  take(client.start());
  const auto until = std::chrono::steady_clock::now() + std::chrono::minutes(15);
  while (std::chrono::steady_clock::now() < until && !std::filesystem::exists(directory + "/stop")) {
    if (client.diagnostics().connected) {
      std::ofstream(directory + "/ready") << "ready";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  client.stop();
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      throw std::runtime_error("usage: psim-command-probe emit|fixture manifest.json");
    }
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    std::ifstream file(args[2]);
    const auto input = Json::parse(file);
    if (std::string_view(args[1]) == "fixture") {
      fixture(input);
    } else {
      asio::io_context io;
      auto future = asio::co_spawn(io, emit(input), asio::use_future);
      io.run();
      future.get();
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
