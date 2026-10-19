#include <google/protobuf/util/time_util.h>
#include <grpc/grpc_security_constants.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h>
#include <grpcpp/support/sync_stream.h>
#include <nlohmann/json.hpp>
#include <psim/connector/v1/connector.pb.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "psim/connector/v1/connector.grpc.pb.h"
#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/security/secret.hpp"

namespace {
namespace wire = psim::connector::v1;

class Receiver final : public wire::ConnectorGatewayService::Service {
 public:
  explicit Receiver(std::size_t count) : seen_(count, false) {}

  grpc::Status Session(grpc::ServerContext* /*context*/,
                       grpc::ServerReaderWriter<wire::SessionResponse, wire::SessionRequest>* stream) override {
    const std::scoped_lock lock(mutex_);
    wire::SessionRequest request;
    if (!stream->Read(&request) || !request.has_register_()) {
      return {grpc::StatusCode::INVALID_ARGUMENT, "registration required"};
    }
    wire::SessionResponse response;
    auto* parameters = response.mutable_registered();
    parameters->set_session_id("load-session");
    parameters->set_protocol_version("1.1");
    parameters->set_initial_credits(10000);
    parameters->set_max_batch_events(1000);
    parameters->set_max_batch_bytes(250000);
    parameters->mutable_heartbeat_interval()->set_seconds(1);
    parameters->mutable_session_timeout()->set_seconds(5);
    if (!stream->Write(response)) {
      return {grpc::StatusCode::UNAVAILABLE, "registration failed"};
    }
    std::uint64_t credits = 10000;
    std::uint64_t batch_sequence = 0;
    while (stream->Read(&request)) {
      response.Clear();
      if (request.has_heartbeat()) {
        *response.mutable_heartbeat() = request.heartbeat();
      } else if (request.has_event_batch()) {
        const auto& batch = request.event_batch();
        if (batch.events_size() < 1 || batch.events_size() > 1000 || batch.ByteSizeLong() > 250000
            || std::cmp_greater(batch.events_size(), credits) || batch.batch_seq() <= batch_sequence) {
          error_ = "invalid batch bounds or credits";
          return {grpc::StatusCode::INVALID_ARGUMENT, error_};
        }
        batch_sequence = batch.batch_seq();
        credits -= static_cast<std::uint64_t>(batch.events_size());
        for (const auto& event : batch.events()) {
          try {
            const auto sequence = std::stoull(event.attributes().at("generator_seq"));
            const auto cursor = std::make_pair(event.source_epoch(), event.source_seq());
            auto& previous = cursors_[event.source_ref()];
            if (sequence < 1 || sequence > seen_.size() || seen_[sequence - 1] || cursor <= previous
                || event.event_id().size() != 36 || event.event_id().at(14) != '7' || !event.has_occurred_at()
                || !google::protobuf::util::TimeUtil::IsTimestampValid(event.occurred_at())) {
              throw std::runtime_error("invalid or repeated generator event");
            }
            seen_[sequence - 1] = true;
            previous = cursor;
            ++count_;
          } catch (const std::exception&) {
            error_ = "event identity, ordering or reconciliation failed";
            return {grpc::StatusCode::INVALID_ARGUMENT, error_};
          }
        }
        response.mutable_batch_ack()->set_batch_seq(batch.batch_seq());
        response.mutable_batch_ack()->set_accepted(static_cast<std::uint32_t>(batch.events_size()));
        if (!stream->Write(response)) {
          return {grpc::StatusCode::UNAVAILABLE, "acknowledgement failed"};
        }
        response.Clear();
        response.mutable_credits()->set_grant(static_cast<std::uint32_t>(batch.events_size()));
        credits += static_cast<std::uint64_t>(batch.events_size());
      } else {
        error_ = "unexpected request";
        return {grpc::StatusCode::INVALID_ARGUMENT, error_};
      }
      if (!stream->Write(response)) {
        break;
      }
    }
    done_.store(count_ == seen_.size());
    return grpc::Status::OK;
  }

  bool done() const { return done_.load(); }

  nlohmann::json report() {
    const std::scoped_lock lock(mutex_);
    return {{"passed", done_.load()},     {"received", count_}, {"expected", seen_.size()},
            {"sources", cursors_.size()}, {"error", error_},    {"receiver", "tls-protocol-peer"}};
  }

 private:
  std::mutex mutex_;
  std::vector<bool> seen_;
  std::map<std::string, std::pair<std::uint32_t, std::uint64_t>> cursors_;
  std::size_t count_ = 0;
  std::atomic_bool done_{false};
  std::string error_;
};

std::string read(const std::filesystem::path& file) {
  std::ifstream input(file);
  return {std::istreambuf_iterator<char>(input), {}};
}
}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): all exceptions are caught at the process boundary
int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      return 2;
    }
    const psim::platform::connector::GrpcProcess grpc;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): argc validates positional arguments
    const std::filesystem::path tls(argv[1]);
    const std::filesystem::path report(argv[2]);
    const auto count = std::stoull(argv[3]);
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (count < 10000 || count > 10000000) {
      throw std::invalid_argument("invalid load count");
    }
    auto key = psim::platform::security::read_secret((tls / "server.key").string());
    if (!key) {
      throw std::runtime_error("load peer key unavailable");
    }
    auto provider = std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    if (!provider->UpdateRoot(read(tls / "ca.pem")).ok()
        || !provider
                ->UpdateIdentityKeyCertPair(std::vector<grpc::experimental::IdentityKeyOrSignerCertPair>{
                    {std::string(key->view()), read(tls / "server.pem")}})
                .ok()) {
      throw std::runtime_error("load peer TLS setup failed");
    }
    auto credentials = grpc::experimental::TlsServerCredentialsOptions::Create(provider);
    if (!credentials.ok()) {
      throw std::runtime_error("load peer credentials failed");
    }
    credentials->set_root_certificate_provider(provider);
    credentials->set_min_tls_version(TLS1_3);
    credentials->set_cert_request_type(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    Receiver receiver(count);
    grpc::ServerBuilder builder;
    builder.RegisterService(&receiver);
    builder.SetMaxReceiveMessageSize(256 * 1024);
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::experimental::TlsServerCredentials(*credentials), &port);
    auto server = builder.BuildAndStart();
    if (!server || port == 0) {
      throw std::runtime_error("load peer startup failed");
    }
    std::ofstream(report / "ready.tmp") << nlohmann::json{{"endpoint", "localhost:" + std::to_string(port)}}.dump();
    std::filesystem::rename(report / "ready.tmp", report / "ready.json");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (!receiver.done() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    server->Shutdown(std::chrono::system_clock::now());
    server->Wait();
    std::ofstream(report / "report.json") << receiver.report().dump(2) << '\n';
    return receiver.done() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
