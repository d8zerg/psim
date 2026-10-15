#include <google/protobuf/util/time_util.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <nlohmann/json.hpp>
#include <psim/connector/v1/connector.pb.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ratio>
#include <stdexcept>
#include <string>
#include <thread>

#include "psim/connector/v1/connector.grpc.pb.h"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/security/secret.hpp"
#include "psim/platform/stream/grpc_process.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/platform/tracing.hpp"

namespace {
std::string pem(const std::string& path) {
  std::ifstream input(path);
  return {(std::istreambuf_iterator<char>(input)), {}};
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const psim::platform::stream::GrpcProcess grpc;
    std::string endpoint = "connector-gateway:8080";
    std::string directory = "/repo/.cache/event-flow";
    std::string code = "ALARM";
    std::string source = "door-1";
    int count = 1;
    int interval = 100;
    int repeat = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): bounded command-line argument index
      const std::string option = argv[i];
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): bounded command-line argument index
      const std::string value = argv[i + 1];
      if (option == "--endpoint") {
        endpoint = value;
      } else if (option == "--tls") {
        directory = value;
      } else if (option == "--count") {
        count = std::stoi(value);
      } else if (option == "--interval-ms") {
        interval = std::stoi(value);
      } else if (option == "--repeat") {
        repeat = std::stoi(value);
      } else if (option == "--code") {
        code = value;
      } else if (option == "--source") {
        source = value;
      } else {
        throw std::runtime_error("unknown simulator option");
      }
    }
    if (count < 1 || count > 1000 || interval < 0 || interval > 60000 || repeat < 1 || repeat > 2) {
      throw std::runtime_error("invalid simulator bounds");
    }
    using namespace psim;
    auto key = platform::security::read_secret(directory + "/client.key");
    if (!key) {
      throw std::runtime_error("client key unavailable");
    }
    grpc::SslCredentialsOptions const tls{pem(directory + "/ca.pem"), std::string(key->view()),
                                          pem(directory + "/client.pem")};
    auto channel = grpc::CreateChannel(endpoint, grpc::SslCredentials(tls));
    auto stub = connector::v1::ConnectorGatewayService::NewStub(channel);
    grpc::ClientContext rpc;
    rpc.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(30)
                     + std::chrono::milliseconds(count * interval));
    auto session = stub->Session(&rpc);
    connector::v1::SessionRequest request;
    connector::v1::SessionResponse response;
    request.mutable_register_()->add_protocol_versions("1.0");
    request.mutable_register_()->set_connector_version("simulator-1");
    request.mutable_register_()->set_instance_id(platform::stream::uuid7());
    if (!session->Write(request) || !session->Read(&response) || !response.has_registered()) {
      throw std::runtime_error("registration failed");
    }
    platform::observability::Tracing tracing({.service = "simulator",
                                              .version = "0.1.0",
                                              .instance = "simulator",
                                              .otlp_endpoint = "otel-collector:4317",
                                              .sampling_ratio = 1,
                                              .otlp_tls = false});
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i) {
      // Open-loop schedule: source time is the intended send time, including generator lateness.
      const auto scheduled = started + std::chrono::milliseconds(i * interval);
      std::this_thread::sleep_until(scheduled);
      const auto late = std::chrono::steady_clock::now() - scheduled;
      const auto wall = std::chrono::system_clock::now() - late;
      const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(wall.time_since_epoch()).count();
      auto trace = tracing.tracer("psim.simulator")->StartSpan("simulate alarm");
      platform::kafka::Headers headers;
      platform::kafka::inject(*trace, headers);
      request.Clear();
      auto* batch = request.mutable_event_batch();
      batch->set_batch_seq(static_cast<std::uint64_t>(i) + 1);
      auto* event = batch->add_events();
      event->set_event_id(platform::stream::uuid7());
      event->set_source_ref(source);
      event->set_source_epoch(1);
      event->set_source_seq(static_cast<std::uint64_t>(i) + 1);
      *event->mutable_occurred_at() = google::protobuf::util::TimeUtil::NanosecondsToTimestamp(nanos);
      event->set_raw_code(code);
      event->set_traceparent(headers.at(0).second);
      for (int attempt = 0; attempt < repeat; ++attempt) {
        if (!session->Write(request) || !session->Read(&response) || !response.has_batch_ack()
            || response.batch_ack().accepted() != 1) {
          throw std::runtime_error("event was not acknowledged");
        }
        if (!session->Read(&response) || !response.has_credits()) {
          throw std::runtime_error("missing credits");
        }
      }
      trace->End();
      std::cout << nlohmann::json{{"event_id", event->event_id()},
                                  {"occurred_ns", nanos},
                                  {"trace_id", event->traceparent().substr(3, 32)},
                                  {"late_ms", std::chrono::duration<double, std::milli>(late).count()}}
                       .dump()
                << '\n';
    }
    session->WritesDone();
    while (session->Read(&response)) {
    }
    if (!session->Finish().ok()) {
      throw std::runtime_error("session failed");
    }
    (void)tracing.flush();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
