#pragma once
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/time_util.h>
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <psim/audit/v1/audit.pb.h>
#include <psim/connector/v1/administration.grpc.pb.h>
#include <psim/connector/v1/connector.grpc.pb.h>
#include <psim/ingest/v1/raw_event.pb.h>
#include <psim/ingest/v1/state.pb.h>
#include <psim/response/v1/command.pb.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "psim/platform/connector/client.hpp"
#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/serde.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::connector_gateway::testing {
namespace asio = boost::asio;
namespace p = platform;
namespace wire = connector::v1;
using Util = google::protobuf::util::TimeUtil;
constexpr std::string_view kTenant = "11111111-1111-4111-8111-111111111111";

inline std::string read(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("test fixture missing");
  }
  return {(std::istreambuf_iterator<char>(input)), {}};
}

inline std::string endpoint(int index = 0) {
  return index == 0 ? "connector-gateway:8080" : "gateway-2:8080";
}

inline std::string identity(int index) {
  return nlohmann::json::parse(read(".cache/gateway-test/identities.json"))
      .at(static_cast<std::size_t>(index))
      .get<std::string>();
}

inline std::string prefix(int index, bool replacement = false) {
  return ".cache/gateway-test/" + std::to_string(index) + (replacement ? "-replacement" : "");
}

inline std::shared_ptr<grpc::Channel> client(int index, int gateway = 0, bool replacement = false) {
  auto value = p::connector::channel({.endpoint = endpoint(gateway),
                                      .ca_file = ".cache/event-flow/ca.pem",
                                      .certificate_file = prefix(index, replacement) + ".pem",
                                      .private_key_file = prefix(index, replacement) + ".key",
                                      .buffer_directory = "unused"});
  if (!value) {
    throw std::runtime_error(value.error().message());
  }
  return *value;
}

inline std::unique_ptr<wire::ConnectorAdministrationService::Stub> admin(int gateway = 0) {
  auto value = p::connector::channel({.endpoint = endpoint(gateway),
                                      .ca_file = ".cache/event-flow/ca.pem",
                                      .certificate_file = ".cache/event-flow/admin.pem",
                                      .private_key_file = ".cache/event-flow/admin.key",
                                      .buffer_directory = "unused"});
  if (!value) {
    throw std::runtime_error(value.error().message());
  }
  return wire::ConnectorAdministrationService::NewStub(*value);
}

inline grpc::Status manage(const wire::ManageRequest& request, wire::ManageResponse* response, int gateway = 0,
                           bool technical = false) {
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(25));
  context.AddMetadata(
      "authorization",
      "Bearer " + read(technical ? ".cache/event-flow/technical.token" : ".cache/event-flow/operator.token"));
  return admin(gateway)->Manage(&context, request, response);
}

inline wire::ManageResponse enroll(int index, std::uint32_t rate = 1000000) {
  wire::ManageRequest request;
  request.set_request_id(p::stream::uuid7());
  request.set_action(wire::REGISTRATION_ACTION_REGISTER);
  auto* value = request.mutable_connector();
  value->set_connector_id(identity(index));
  value->set_name("Synthetic connector");
  value->set_connector_type("test");
  value->mutable_limits()->set_max_events_per_second(rate);
  value->mutable_limits()->set_max_batch_events(1000);
  request.set_certificate_pem(read(prefix(index) + ".pem"));
  wire::ManageResponse response;
  auto status = manage(request, &response);
  if (!status.ok()) {
    throw std::runtime_error("registration failed: " + status.error_message());
  }
  return response;
}

struct Stream {
  grpc::ClientContext context;
  std::unique_ptr<wire::ConnectorGatewayService::Stub> stub;
  std::unique_ptr<grpc::ClientReaderWriter<wire::SessionRequest, wire::SessionResponse>> stream;
  wire::Registered registered;
  std::uint64_t sequence = 0;

  explicit Stream(int index, int gateway = 0, bool replacement = false)
      : stub(wire::ConnectorGatewayService::NewStub(client(index, gateway, replacement))) {
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(45));
    stream = stub->Session(&context);
  }

  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;
  Stream(Stream&&) = delete;
  Stream& operator=(Stream&&) = delete;

  ~Stream() {
    context.TryCancel();
    stream->WritesDone();
    (void)stream->Finish();
  }

  bool connect(bool state = true) {
    wire::SessionRequest request;
    auto* value = request.mutable_register_();
    value->add_protocol_versions(state ? "1.1" : "1.0");
    value->set_connector_version("test");
    value->set_instance_id("network-test");
    if (state) {
      value->add_capabilities("state.v1");
      value->add_capabilities("snapshot.v1");
      value->add_capabilities("commands.v1");
    }
    if (!stream->Write(request)) {
      return false;
    }
    wire::SessionResponse response;
    if (!stream->Read(&response) || !response.has_registered()) {
      return false;
    }
    registered = response.registered();
    return true;
  }

  bool next(wire::SessionResponse* response, wire::SessionResponse::MessageCase type) const {
    while (stream->Read(response)) {
      if (response->message_case() == type) {
        return true;
      }
    }
    return false;
  }

  bool heartbeat() const {
    wire::SessionRequest request;
    *request.mutable_heartbeat()->mutable_sent_at() = Util::GetCurrentTime();
    if (!stream->Write(request)) {
      return false;
    }
    wire::SessionResponse response;
    return next(&response, wire::SessionResponse::kHeartbeat);
  }

  wire::BatchAck batch(const std::vector<wire::ConnectorEvent>& events) {
    wire::SessionRequest request;
    request.mutable_event_batch()->set_batch_seq(++sequence);
    for (const auto& event : events) {
      *request.mutable_event_batch()->add_events() = event;
    }
    if (!stream->Write(request)) {
      throw std::runtime_error("test batch write failed");
    }
    wire::SessionResponse response;
    if (!next(&response, wire::SessionResponse::kBatchAck)) {
      throw std::runtime_error("test batch was not acknowledged");
    }
    auto ack = response.batch_ack();
    if (!next(&response, wire::SessionResponse::kCredits)) {
      throw std::runtime_error("test batch did not return credits");
    }
    return ack;
  }
};

inline wire::ConnectorEvent event(std::uint64_t sequence = 1) {
  wire::ConnectorEvent input;
  input.set_event_id(p::stream::uuid7());
  input.set_source_ref("door/alarm");
  input.set_source_epoch(1);
  input.set_source_seq(sequence);
  input.set_raw_code("ALARM");
  *input.mutable_occurred_at() = Util::GetCurrentTime();
  return input;
}

template <typename T>
T run(asio::awaitable<T> operation) {
  asio::io_context io;
  auto future = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.run();
  return future.get();
}

inline asio::awaitable<void> send_command(response::v1::CommandDeliveryRecord command) {
  auto executor = co_await asio::this_coro::executor;
  auto registry = p::kafka::SchemaRegistry::create(
      {.url = "http://registry:8080/apis/ccompat/v7", .http = {.allow_plaintext = true}});
  if (!registry) {
    throw std::runtime_error("test schema client unavailable");
  }
  auto schema = co_await registry->latest_id("psim.commands.v1-value");
  if (!schema) {
    throw std::runtime_error("test command schema missing");
  }
  auto producer = p::kafka::Producer::create(
      executor, {.client = {.bootstrap = "kafka-1:19092", .client_id = "gateway-test", .allow_plaintext = true}});
  if (!producer) {
    throw std::runtime_error("test command producer unavailable");
  }
  std::string bytes;
  p::kafka::wire::write_header(
      bytes, *schema, p::kafka::wire::message_indexes(*psim::response::v1::CommandDeliveryRecord::GetDescriptor()));
  command.AppendToString(&bytes);
  auto sent = co_await (*producer)->send(
      {.topic = "psim.commands.v1", .key = command.command_delivery().connector_id(), .value = std::move(bytes)});
  if (!sent) {
    throw std::runtime_error("test command delivery failed");
  }
}

enum class StatePayload : std::uint8_t { kReport, kOnline, kOffline };

template <typename Message>
asio::awaitable<Message> find(std::string topic, std::string connector, std::string event_id = {},
                              StatePayload payload = StatePayload::kReport) {
  auto executor = co_await asio::this_coro::executor;
  auto consumer = p::kafka::Consumer::create(
      executor, {.client = {.bootstrap = "kafka-1:19092", .client_id = "gateway-test-reader", .allow_plaintext = true},
                 .group_id = "gateway-test-" + p::stream::uuid7(),
                 .topics = {topic},
                 .group_protocol = "classic"});
  if (!consumer) {
    throw std::runtime_error("test reader unavailable");
  }
  std::optional<Message> result;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
  while (!result && std::chrono::steady_clock::now() < deadline) {
    auto batch = co_await (*consumer)->poll(1000, std::chrono::milliseconds(100));
    if (!batch) {
      throw std::runtime_error("test reader failed");
    }
    for (const auto& item : *batch) {
      auto value = p::kafka::ProtobufSerde<Message>(1).deserialize(item.value());
      if (!value) {
        continue;
      }
      if constexpr (std::is_same_v<Message, ingest::v1::RawEventRecord>) {
        if (value->raw_event().connector_id() == connector && value->envelope().message_id() == event_id) {
          result = std::move(*value);
        }
      } else if constexpr (std::is_same_v<Message, audit::v1::AuditEventRecord>) {
        if (value->has_audit_entry() && value->audit_entry().object_id() == connector
            && value->audit_entry().action() == event_id) {
          result = std::move(*value);
        }
      } else if constexpr (std::is_same_v<Message, response::v1::CommandResultRecord>) {
        if (value->has_command_outcome() && value->command_outcome().command_id() == event_id) {
          result = std::move(*value);
        }
      } else {
        const bool selected =
            payload == StatePayload::kReport
                ? value->has_state_report()
                : value->has_connection() && value->connection().online() == (payload == StatePayload::kOnline);
        if (value->connector_id() == connector && value->session_id() == event_id && selected) {
          result = std::move(*value);
        }
      }
    }
  }
  co_await (*consumer)->close();
  if (!result) {
    throw std::runtime_error("expected durable test record not found");
  }
  co_return std::move(*result);
}

}  // namespace psim::connector_gateway::testing
