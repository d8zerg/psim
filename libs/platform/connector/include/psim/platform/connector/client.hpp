#pragma once

#include <grpcpp/channel.h>
#include <psim/connector/v1/connector.pb.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

#include "psim/platform/error.hpp"

namespace psim::platform::connector {
namespace wire = ::psim::connector::v1;

enum class Observation : std::uint8_t { kHistorical, kCurrent };

struct Options {
  std::string endpoint;
  std::string ca_file;
  std::string certificate_file;
  std::string private_key_file;
  std::string buffer_directory;
  std::string connector_version = "1.0";
  std::size_t max_buffer_bytes = std::size_t{64} * 1024 * 1024;
  std::size_t max_pending_events = 100000;
  std::size_t max_commands = 10000;
  std::chrono::milliseconds reconnect_min{100};
  std::chrono::milliseconds reconnect_max{10000};
  std::chrono::milliseconds registration_timeout{10000};
  std::chrono::milliseconds acknowledgement_timeout{30000};
};

struct Diagnostics {
  bool connected = false;
  std::size_t pending_events = 0;
  std::size_t buffer_bytes = 0;
  std::size_t sessions = 0;
  std::size_t acknowledged = 0;
  std::size_t rejected = 0;
  std::string last_error;
};

struct Callbacks {
  /// Runs on a separate command worker. Honor stop and the command deadline; do not detach work.
  std::function<wire::CommandResult(const wire::Command&, std::stop_token)> command{};
  /// Called on registration. Capture equipment state after the supplied sequence boundaries.
  /// Report only sources that were actually queried; an absent snapshot leaves state unknown.
  std::function<std::vector<wire::StateObservation>(const std::vector<wire::SourceCursor>&, std::stop_token)>
      snapshot{};
  std::function<void(const wire::ConfigUpdate&)> config{};
  std::function<void(const wire::EventRejection&)> rejection{};
};

/// Creates verified TLS 1.3 mTLS credentials; no plaintext or hostname override is exposed.
[[nodiscard]] Result<std::shared_ptr<grpc::Channel>> channel(const Options& options);

/// One process exclusively owns its durable directory. publish() fsyncs before returning success.
/// Create one GrpcProcess in main before clients; destroy it after every gRPC object.
class Client final {
 public:
  explicit Client(Options options, Callbacks callbacks = {});
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;

  [[nodiscard]] Result<void> start();
  void stop();
  /// SDK assigns UUIDv7 and a durable sequence; source_epoch defaults to 1 and must never decrease.
  [[nodiscard]] Result<wire::ConnectorEvent> publish(wire::ConnectorEvent event,
                                                     Observation observation = Observation::kHistorical);
  /// Current equipment connectivity/values; only accepted while state.v1 is negotiated.
  [[nodiscard]] Result<void> report(wire::StateObservation observation);
  [[nodiscard]] bool flush(std::chrono::milliseconds timeout);
  [[nodiscard]] Diagnostics diagnostics() const;
  /// Prometheus text format; serve with the connector's existing diagnostics endpoint.
  [[nodiscard]] std::string metrics() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace psim::platform::connector
