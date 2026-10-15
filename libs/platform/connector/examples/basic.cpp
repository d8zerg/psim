#include <google/protobuf/util/time_util.h>
#include <psim/connector/v1/connector.pb.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "psim/platform/connector/client.hpp"
#include "psim/platform/connector/grpc_process.hpp"

namespace {
std::string setting(const char* name) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): the process never mutates its environment
  const auto* value = std::getenv(name);
  if (value == nullptr) {
    throw std::runtime_error("missing example configuration");
  }
  return value;
}
}  // namespace

int main() {
  try {
    namespace sdk = psim::platform::connector;
    namespace wire = psim::connector::v1;
    const sdk::GrpcProcess grpc;
    const sdk::Options options{.endpoint = setting("PSIM_CONNECTOR_ENDPOINT"),
                               .ca_file = setting("PSIM_CONNECTOR_CA_FILE"),
                               .certificate_file = setting("PSIM_CONNECTOR_CERTIFICATE_FILE"),
                               .private_key_file = setting("PSIM_CONNECTOR_PRIVATE_KEY_FILE"),
                               .buffer_directory = setting("PSIM_CONNECTOR_BUFFER_DIRECTORY")};
    std::atomic_int executions{0};
    sdk::Client client(options,
                       {.command =
                            [&](const wire::Command& command, const std::stop_token& stop) {
                              wire::CommandResult result;
                              result.set_success(!stop.stop_requested() && command.capability() == "arm"
                                                 && command.device_external_id() == "device-1");
                              if (!result.success()) {
                                result.set_code("COMMAND_CAPABILITY_UNSUPPORTED");
                              }
                              ++executions;
                              return result;
                            },
                        .snapshot =
                            [](const std::vector<wire::SourceCursor>& /*boundaries*/, const std::stop_token& /*stop*/) {
                              wire::StateObservation state;
                              state.mutable_cursor()->set_source_ref("door-1");
                              state.set_availability(wire::DEVICE_AVAILABILITY_ONLINE);
                              *state.mutable_observed_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
                              (*state.mutable_values())["armed"] = "true";
                              return std::vector{state};
                            }});
    auto started = client.start();
    if (!started) {
      throw std::runtime_error(started.error().message());
    }
    for (int i = 0; i < 20; ++i) {
      wire::ConnectorEvent event;
      event.set_source_ref("door-1");
      event.set_raw_code(i == 19 ? "REJECT" : "ALARM");
      auto stored = client.publish(std::move(event), sdk::Observation::kCurrent);
      if (!stored) {
        throw std::runtime_error(stored.error().message());
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!client.flush(std::chrono::seconds(std::stoi(setting("PSIM_CONNECTOR_FLUSH_TIMEOUT_SECONDS"))))) {
      throw std::runtime_error("example did not drain its durable buffer");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    client.stop();
    std::cout << client.metrics();
    return executions.load() <= 1 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
