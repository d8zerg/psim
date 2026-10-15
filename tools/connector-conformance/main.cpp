#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/util/message_differencer.h>
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "psim/connector/v1/connector.grpc.pb.h"
#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/security/secret.hpp"

namespace {
namespace wire = psim::connector::v1;
using Clock = std::chrono::steady_clock;
using Stream = grpc::ServerReaderWriter<wire::SessionResponse, wire::SessionRequest>;

std::string read(const std::filesystem::path& file) {
  std::ifstream input(file);
  return {std::istreambuf_iterator<char>(input), {}};
}

void require(bool condition, const char* error) {
  if (!condition) {
    throw std::runtime_error(error);
  }
}

class Gateway final : public wire::ConnectorGatewayService::Service {
 public:
  explicit Gateway(int outage) : outage_(outage) {}

  grpc::Status Session(grpc::ServerContext* /*context*/, Stream* stream) override {
    const std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      return {grpc::StatusCode::RESOURCE_EXHAUSTED, "one conformance session at a time"};
    }
    try {
      wire::SessionRequest request;
      require(stream->Read(&request) && request.has_register_(), "Register must be first");
      if (dropped_ && Clock::now() < restore_) {
        return {grpc::StatusCode::UNAVAILABLE, "conformance link outage"};
      }
      require(std::ranges::find(request.register_().protocol_versions(), "1.1")
                  != request.register_().protocol_versions().end(),
              "fixture must support protocol 1.1");
      const auto capabilities = request.register_().capabilities();
      const bool state = std::ranges::find(capabilities, "state.v1") != capabilities.end();
      const bool snapshot_capable = state && std::ranges::find(capabilities, "snapshot.v1") != capabilities.end();
      const auto registered_at = google::protobuf::util::TimeUtil::GetCurrentTime();
      const bool commands = std::ranges::find(capabilities, "commands.v1") != capabilities.end();
      ++sessions_;
      wire::SessionResponse response;
      auto* parameters = response.mutable_registered();
      parameters->set_session_id("conformance-" + std::to_string(sessions_));
      parameters->set_connector_id("22222222-2222-4222-8222-222222222222");
      parameters->set_protocol_version("1.1");
      parameters->set_max_batch_events(2);
      parameters->set_max_batch_bytes(65536);
      parameters->mutable_heartbeat_interval()->set_nanos(50000000);
      parameters->mutable_session_timeout()->set_seconds(1);
      if (state) {
        parameters->add_capabilities("state.v1");
      }
      if (snapshot_capable) {
        parameters->add_capabilities("snapshot.v1");
      }
      if (commands) {
        parameters->add_capabilities("commands.v1");
      }
      require(stream->Write(response), "Registered write failed");
      std::uint64_t credits = 0;
      bool granted = false;
      std::set<std::string> results;
      std::size_t duplicate_results = 0;
      bool snapshot = !snapshot_capable;
      std::uint64_t last_batch = 0;
      while (stream->Read(&request)) {
        response.Clear();
        if (request.has_heartbeat()) {
          *response.mutable_heartbeat() = request.heartbeat();
          require(stream->Write(response), "heartbeat response failed");
          if (!granted) {
            response.Clear();
            response.mutable_credits()->set_grant(2);
            credits += 2;
            require(stream->Write(response), "credit grant failed");
            granted = true;
            response.Clear();
            response.mutable_config_update()->set_config_version(1);
            require(stream->Write(response) && stream->Write(response), "configuration delivery failed");
            if (dropped_ && commands) {
              response.Clear();
              auto* command = response.mutable_command();
              command->set_command_id("33333333-3333-4333-8333-333333333333");
              command->set_device_external_id("device-1");
              command->set_capability("arm");
              *command->mutable_deadline_at() =
                  google::protobuf::util::TimeUtil::TimeTToTimestamp(std::time(nullptr) + 30);
              require(stream->Write(response) && stream->Write(response), "duplicate command delivery failed");
              command->set_command_id("44444444-4444-4444-8444-444444444444");
              command->mutable_deadline_at()->set_seconds(1);
              require(stream->Write(response), "expired command delivery failed");
            }
          }
        } else if (request.has_event_batch()) {
          const auto& batch = request.event_batch();
          require(batch.batch_seq() > last_batch, "batch sequence did not increase within session");
          last_batch = batch.batch_seq();
          require(granted && batch.events_size() > 0 && batch.events_size() <= 2
                      && std::cmp_less_equal(batch.events_size(), credits) && batch.ByteSizeLong() <= 65536,
                  "batch exceeded granted credits or negotiated bounds");
          credits -= static_cast<std::uint64_t>(batch.events_size());
          auto* ack = response.mutable_batch_ack();
          ack->set_batch_seq(batch.batch_seq());
          std::uint32_t accepted = 0;
          for (const auto& event : batch.events()) {
            require(
                event.event_id().size() == 36 && event.event_id().at(14) == '7'
                    && boost::uuids::to_string(boost::uuids::string_generator()(event.event_id())) == event.event_id()
                    && event.source_seq() > 0 && event.source_epoch() > 0 && event.source_ref() == "door-1"
                    && event.has_occurred_at()
                    && google::protobuf::util::TimeUtil::IsTimestampValid(event.occurred_at())
                    && (event.raw_code() == "ALARM" || event.raw_code() == "REJECT"),
                "invalid fixture event");
            auto original = event;
            original.set_live_observation(false);
            const auto [item, inserted] = payloads_.try_emplace(event.event_id(), original);
            require(inserted || google::protobuf::util::MessageDifferencer::Equals(item->second, original),
                    "replay changed event identity or payload");
            if (inserted) {
              const auto cursor = std::make_tuple(event.source_epoch(), event.source_seq());
              require(cursor > last_cursor_, "new source events are not strictly ordered");
              last_cursor_ = cursor;
            }
            if (dropped_) {
              require(!event.live_observation(), "buffer replay claimed to be a live observation");
              if (event.event_id() == unacknowledged_) {
                require(event.source_seq() == unacknowledged_sequence_, "replay changed source sequence");
                replayed_ = true;
              }
            }
            events_.insert(event.event_id());
            require(events_.size() <= 20, "fixture emitted too many unique events");
            if (event.raw_code() == "REJECT") {
              auto* rejection = ack->add_rejections();
              rejection->set_event_id(event.event_id());
              rejection->set_code("INGEST_INVALID_EVENT");
              ++rejections_;
            } else {
              ++accepted;
            }
          }
          if (!dropped_) {
            dropped_ = true;
            unacknowledged_ = batch.events(0).event_id();
            unacknowledged_sequence_ = batch.events(0).source_seq();
            restore_ = Clock::now() + std::chrono::seconds(outage_);
            return {grpc::StatusCode::UNAVAILABLE, "dropped before BatchAck"};
          }
          ack->set_accepted(accepted);
          require(stream->Write(response), "BatchAck write failed");
          response.Clear();
          response.mutable_credits()->set_grant(static_cast<std::uint32_t>(batch.events_size()));
          credits += static_cast<std::uint64_t>(batch.events_size());
          require(stream->Write(response), "credit replenishment failed");
        } else if (request.has_command_result()) {
          const auto& result = request.command_result();
          if (result.command_id() == "33333333-3333-4333-8333-333333333333") {
            require(result.success(), "fixture arm command failed");
            ++duplicate_results;
          } else {
            require(result.command_id() == "44444444-4444-4444-8444-444444444444" && result.code() == "COMMAND_EXPIRED",
                    "expired command was not rejected");
          }
          results.insert(result.command_id());
        } else if (request.has_state_report()) {
          require(state, "unnegotiated state feature used");
          for (const auto& observation : request.state_report().observations()) {
            require(observation.has_observed_at() && observation.cursor().source_ref() == "door-1"
                        && observation.cursor().source_epoch() > 0 && observation.cursor().source_seq() <= 20
                        && observation.observed_at().seconds() >= registered_at.seconds()
                        && google::protobuf::util::TimeUtil::IsTimestampValid(observation.observed_at()),
                    "snapshot used stale observations or an invalid source boundary");
          }
          if (request.state_report().snapshot() && request.state_report().complete()) {
            snapshot = true;
          }
        } else {
          require(request.has_command_ack(), "unexpected fixture frame");
        }
        if (events_.size() == 20 && replayed_ && rejections_ == 1 && snapshot
            && (!commands || (results.size() == 2 && duplicate_results == 2))) {
          passed_.store(true);
          return grpc::Status::OK;
        }
      }
      return grpc::Status::OK;
    } catch (const std::exception& error) {
      error_ = error.what();
      failed_.store(true);
      return {grpc::StatusCode::INVALID_ARGUMENT, error_};
    }
  }

  nlohmann::json report() {
    const std::scoped_lock lock(mutex_);
    return {{"passed", passed_.load()}, {"outage_seconds", outage_},      {"unique_events", events_.size()},
            {"sessions", sessions_},    {"lost_ack_replayed", replayed_}, {"permanent_rejections", rejections_},
            {"error", error_}};
  }

  bool passed() const { return passed_.load(); }

  bool failed() const { return failed_.load(); }

 private:
  std::atomic_bool passed_{false}, failed_{false};
  int outage_;
  std::mutex mutex_;
  bool dropped_ = false, replayed_ = false;
  Clock::time_point restore_{};
  std::set<std::string> events_;
  std::map<std::string, wire::ConnectorEvent> payloads_;
  std::tuple<std::uint32_t, std::uint64_t> last_cursor_{0, 0};
  std::size_t sessions_ = 0, rejections_ = 0;
  std::string unacknowledged_, error_;
  std::uint64_t unacknowledged_sequence_ = 0;
};
}  // namespace

int main(int argc, char** argv) {
  try {
    const psim::platform::connector::GrpcProcess grpc;
    require(argc == 4, "usage: psim-connector-conformance TLS_DIRECTORY REPORT_DIRECTORY OUTAGE_SECONDS");
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): argc checks all argument indices
    const std::filesystem::path tls(argv[1]);
    const std::filesystem::path reports(argv[2]);
    const int outage = std::stoi(argv[3]);
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    require(outage >= 1 && outage <= 600, "invalid outage duration");
    auto key = psim::platform::security::read_secret((tls / "server.key").string());
    require(key.has_value(), "test server key unavailable");
    auto provider = std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    require(provider->UpdateRoot(read(tls / "ca.pem")).ok(), "invalid test CA");
    require(provider
                ->UpdateIdentityKeyCertPair(std::vector<grpc::experimental::IdentityKeyOrSignerCertPair>{
                    {std::string(key->view()), read(tls / "server.pem")}})
                .ok(),
            "invalid test server identity");
    auto credentials = grpc::experimental::TlsServerCredentialsOptions::Create(provider);
    require(credentials.ok(), "invalid server credentials");
    credentials->set_root_certificate_provider(provider);
    credentials->set_min_tls_version(TLS1_3);
    credentials->set_cert_request_type(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    Gateway gateway(outage);
    grpc::ServerBuilder builder;
    builder.RegisterService(&gateway);
    builder.SetMaxReceiveMessageSize(256 * 1024);
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::experimental::TlsServerCredentials(*credentials), &port);
    auto server = builder.BuildAndStart();
    require(server != nullptr && port > 0, "cannot start conformance peer");
    std::ofstream(reports / "ready.tmp") << nlohmann::json{{"endpoint", "localhost:" + std::to_string(port)}}.dump();
    std::filesystem::rename(reports / "ready.tmp", reports / "ready.json");
    const auto end = Clock::now() + std::chrono::seconds(outage + 45);
    while (!gateway.passed() && !gateway.failed() && Clock::now() < end) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    server->Shutdown(std::chrono::system_clock::now());
    server->Wait();
    const auto report = gateway.report();
    std::ofstream(reports / "report.json") << report.dump(2) << '\n';
    return gateway.passed() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
