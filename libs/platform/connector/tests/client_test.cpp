#include "psim/platform/connector/client.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <google/protobuf/util/time_util.h>
#include <grpc/grpc_security_constants.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h>
#include <grpcpp/support/sync_stream.h>
#include <gtest/gtest.h>
#include <psim/connector/v1/connector.pb.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../security/tests/support.hpp"
#include "../src/store.hpp"
#include "psim/connector/v1/connector.grpc.pb.h"
#include "psim/platform/connector/state.hpp"

namespace psim::platform::connector {
namespace {
namespace fixture = ::psim::platform::security::testing;
using Stream = grpc::ServerReaderWriter<wire::SessionResponse, wire::SessionRequest>;

bool wait(const std::function<bool()>& condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
  const auto end = std::chrono::steady_clock::now() + timeout;
  while (!condition() && std::chrono::steady_clock::now() < end) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return condition();
}

wire::SessionResponse registered(std::string id = "session", std::uint32_t credits = 1) {
  wire::SessionResponse response;
  auto* value = response.mutable_registered();
  value->set_session_id(std::move(id));
  value->set_connector_id("22222222-2222-4222-8222-222222222222");
  value->set_protocol_version("1.1");
  value->set_initial_credits(credits);
  value->set_max_batch_events(2);
  value->set_max_batch_bytes(65536);
  value->mutable_heartbeat_interval()->set_nanos(50000000);
  value->mutable_session_timeout()->set_seconds(1);
  return response;
}

bool receive(Stream* stream, wire::SessionRequest* request) {
  while (stream->Read(request)) {
    if (!request->has_heartbeat()) {
      return true;
    }
    wire::SessionResponse response;
    *response.mutable_heartbeat() = request->heartbeat();
    if (!stream->Write(response)) {
      return false;
    }
  }
  return false;
}

void acknowledge(Stream* stream, const wire::EventBatch& batch) {
  wire::SessionResponse response;
  response.mutable_batch_ack()->set_batch_seq(batch.batch_seq());
  response.mutable_batch_ack()->set_accepted(static_cast<std::uint32_t>(batch.events_size()));
  (void)stream->Write(response);
  response.Clear();
  response.mutable_credits()->set_grant(static_cast<std::uint32_t>(batch.events_size()));
  (void)stream->Write(response);
}

wire::ConnectorEvent alarm() {
  wire::ConnectorEvent event;
  event.set_source_ref("door-1");
  event.set_raw_code("ALARM");
  return event;
}

class Peer final : public wire::ConnectorGatewayService::Service {
 public:
  explicit Peer(std::function<grpc::Status(grpc::ServerContext*, Stream*)> scenario) : scenario_(std::move(scenario)) {
    auto provider = std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    fixture::require(provider->UpdateRoot(fixture::certificate_pem(tls.ca.native.get())).ok());
    fixture::require(provider
                         ->UpdateIdentityKeyCertPair(std::vector<grpc::experimental::IdentityKeyOrSignerCertPair>{
                             {fixture::private_pem(tls.server.private_key.get()),
                              fixture::certificate_pem(tls.server.native.get())}})
                         .ok());
    auto options = grpc::experimental::TlsServerCredentialsOptions::Create(provider);
    fixture::require(options.ok());
    options->set_root_certificate_provider(provider);
    options->set_min_tls_version(TLS1_3);
    options->set_cert_request_type(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    grpc::ServerBuilder builder;
    builder.RegisterService(this);
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::experimental::TlsServerCredentials(*options), &port);
    server_ = builder.BuildAndStart();
    fixture::require(server_ != nullptr && port > 0);
    endpoint = "localhost:" + std::to_string(port);
  }

  Peer(const Peer&) = delete;
  Peer& operator=(const Peer&) = delete;
  Peer(Peer&&) = delete;
  Peer& operator=(Peer&&) = delete;

  ~Peer() override {
    server_->Shutdown(std::chrono::system_clock::now());
    server_->Wait();
  }

  grpc::Status Session(grpc::ServerContext* context, Stream* stream) override { return scenario_(context, stream); }

  Options options() {
    const auto files = tls.options(tls.client, "client");
    return {.endpoint = endpoint,
            .ca_file = files.ca_file,
            .certificate_file = files.certificate_file,
            .private_key_file = files.private_key_file,
            .buffer_directory = (tls.directory.path / "buffer").string(),
            .reconnect_min = std::chrono::milliseconds(10),
            .reconnect_max = std::chrono::milliseconds(50),
            .registration_timeout = std::chrono::milliseconds(300)};
  }

  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes): shared test fixture material
  fixture::Fixture tls;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes): loopback fixture endpoint
  std::string endpoint;

 private:
  std::function<grpc::Status(grpc::ServerContext*, Stream*)> scenario_;
  std::unique_ptr<grpc::Server> server_;
};

TEST(ConnectorConformance, CreditsBeforeAcknowledgementRestoreTheFullBatchWindow) {
  std::atomic_bool first_received{false};
  std::atomic_bool queued{false};
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request) || !stream->Write(registered("session", 2))) {
      return grpc::Status::OK;
    }
    for (const auto expected : {1, 2, 2}) {
      if (!receive(stream, &request) || !request.has_event_batch()) {
        return grpc::Status::OK;
      }
      EXPECT_EQ(request.event_batch().events_size(), expected);
      if (expected == 1) {
        first_received.store(true);
        if (!wait([&] { return queued.load(); })) {
          return grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED, "events were not queued");
        }
      }
      wire::SessionResponse response;
      response.mutable_credits()->set_grant(static_cast<std::uint32_t>(request.event_batch().events_size()));
      (void)stream->Write(response);
      response.Clear();
      response.mutable_batch_ack()->set_batch_seq(request.event_batch().batch_seq());
      response.mutable_batch_ack()->set_accepted(static_cast<std::uint32_t>(request.event_batch().events_size()));
      (void)stream->Write(response);
    }
    while (receive(stream, &request)) {
    }
    return grpc::Status::OK;
  });
  Client client(peer.options());
  ASSERT_TRUE(client.publish(alarm()));
  ASSERT_TRUE(client.start());
  ASSERT_TRUE(wait([&] { return first_received.load(); }));
  ASSERT_TRUE(client.publish_batch({alarm(), alarm(), alarm(), alarm()}));
  queued.store(true);
  EXPECT_TRUE(client.flush(std::chrono::milliseconds(5000)));
  EXPECT_EQ(client.diagnostics().acknowledged, 5);
  EXPECT_EQ(client.diagnostics().batch_acks, 3);
  client.stop();
}

TEST(ConnectorConformance, CreditsHeartbeatLostAckReplayAndLegacyNegotiation) {
  std::atomic_int sessions{0};
  std::string first_id;
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    EXPECT_TRUE(request.has_register_());
    const auto attempt = ++sessions;
    auto parameters = registered("session-" + std::to_string(attempt), 0);
    parameters.mutable_registered()->set_protocol_version("1.0");
    if (!stream->Write(parameters)) {
      return grpc::Status::OK;
    }
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    EXPECT_TRUE(request.has_heartbeat()) << "event sent without credits";
    wire::SessionResponse response;
    *response.mutable_heartbeat() = request.heartbeat();
    (void)stream->Write(response);
    response.Clear();
    response.mutable_credits()->set_grant(1);
    (void)stream->Write(response);
    while (receive(stream, &request)) {
      EXPECT_TRUE(request.has_event_batch());
      if (!request.has_event_batch()) {
        break;
      }
      EXPECT_EQ(request.event_batch().events_size(), 1);
      const auto& event = request.event_batch().events(0);
      EXPECT_FALSE(event.live_observation());
      if (attempt == 1) {
        first_id = event.event_id();
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "lost acknowledgement");
      }
      EXPECT_EQ(event.event_id(), first_id);
      acknowledge(stream, request.event_batch());
      break;
    }
    while (receive(stream, &request)) {
    }
    return grpc::Status::OK;
  });
  Client client(peer.options());
  ASSERT_TRUE(client.publish(alarm()));
  ASSERT_TRUE(client.start());
  EXPECT_TRUE(client.flush(std::chrono::milliseconds(5000)));
  EXPECT_GE(sessions.load(), 2);
  EXPECT_EQ(client.diagnostics().acknowledged, 1);
  EXPECT_GE(client.diagnostics().batches_sent, 2);
  EXPECT_EQ(client.diagnostics().batch_acks, 1);
  EXPECT_GT(client.diagnostics().max_acknowledgement_ms, 0);
  EXPECT_GT(client.diagnostics().max_buffer_acknowledgement_ms, 0);
  EXPECT_GT(client.diagnostics().max_pending_read_ms, 0);
  EXPECT_NE(client.metrics().find("psim_connector_pending_events 0"), std::string::npos);
  EXPECT_FALSE(client.start());
  client.stop();
  EXPECT_FALSE(client.diagnostics().connected);
}

TEST(ConnectorConformance, InvalidAckDoesNotDeletePendingEvent) {
  std::atomic_int sessions{0};
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    const int attempt = ++sessions;
    (void)stream->Write(registered("attempt-" + std::to_string(attempt)));
    if (!receive(stream, &request) || !request.has_event_batch()) {
      return grpc::Status::OK;
    }
    if (attempt == 1) {
      wire::SessionResponse response;
      response.mutable_batch_ack()->set_batch_seq(request.event_batch().batch_seq() + 1);
      response.mutable_batch_ack()->set_accepted(1);
      (void)stream->Write(response);
      while (stream->Read(&request)) {
      }
    } else {
      acknowledge(stream, request.event_batch());
      while (receive(stream, &request)) {
      }
    }
    return grpc::Status::OK;
  });
  Client client(peer.options());
  ASSERT_TRUE(client.publish(alarm()));
  ASSERT_TRUE(client.start());
  EXPECT_TRUE(client.flush(std::chrono::milliseconds(5000)));
  EXPECT_GE(sessions.load(), 2);
}

TEST(ConnectorConformance, PermanentRejectionsAreReportedAndNotResent) {
  std::atomic_int rejected{0};
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    (void)stream->Write(registered());
    if (!receive(stream, &request) || !request.has_event_batch()) {
      return grpc::Status::OK;
    }
    wire::SessionResponse response;
    auto* ack = response.mutable_batch_ack();
    ack->set_batch_seq(request.event_batch().batch_seq());
    auto* rejection = ack->add_rejections();
    rejection->set_event_id(request.event_batch().events(0).event_id());
    rejection->set_code("INGEST_INVALID_EVENT");
    (void)stream->Write(response);
    while (receive(stream, &request)) {
    }
    return grpc::Status::OK;
  });
  Client client(peer.options(), {.rejection = [&](const wire::EventRejection& rejection) {
                  EXPECT_EQ(rejection.code(), "INGEST_INVALID_EVENT");
                  ++rejected;
                }});
  ASSERT_TRUE(client.publish(alarm()));
  ASSERT_TRUE(client.start());
  EXPECT_TRUE(client.flush(std::chrono::milliseconds(5000)));
  EXPECT_EQ(rejected.load(), 1);
  EXPECT_EQ(client.diagnostics().rejected, 1);
}

TEST(ConnectorConformance, DuplicateAndExpiredCommandsAndVersionedConfiguration) {
  std::atomic_int executions{0};
  std::atomic_int configs{0};
  std::atomic_int results{0};
  const auto command_id = detail::uuid7();
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    auto parameters = registered();
    parameters.mutable_registered()->add_capabilities("commands.v1");
    (void)stream->Write(parameters);
    wire::SessionResponse response;
    response.mutable_config_update()->set_config_version(2);
    (void)stream->Write(response);
    (void)stream->Write(response);
    response.Clear();
    auto* command = response.mutable_command();
    command->set_command_id(command_id);
    command->set_device_external_id("device-1");
    command->set_capability("arm");
    command->mutable_deadline_at()->set_seconds(253402300799);
    for (int i = 0; i < 2; ++i) {
      (void)stream->Write(response);
      while (receive(stream, &request)) {
        if (request.has_command_result()) {
          EXPECT_TRUE(request.command_result().success());
          ++results;
          break;
        }
        EXPECT_TRUE(request.has_command_ack());
      }
    }
    command->set_command_id(detail::uuid7());
    command->mutable_deadline_at()->set_seconds(1);
    (void)stream->Write(response);
    while (receive(stream, &request)) {
      if (request.has_command_result()) {
        EXPECT_EQ(request.command_result().code(), "COMMAND_EXPIRED");
        ++results;
      }
    }
    return grpc::Status::OK;
  });
  Client client(peer.options(), {.command =
                                     [&](const wire::Command& command, const std::stop_token& /*stop*/) {
                                       EXPECT_EQ(command.command_id(), command_id);
                                       ++executions;
                                       wire::CommandResult result;
                                       result.set_success(true);
                                       return result;
                                     },
                                 .config =
                                     [&](const wire::ConfigUpdate& update) {
                                       EXPECT_EQ(update.config_version(), 2);
                                       ++configs;
                                     }});
  ASSERT_TRUE(client.start());
  ASSERT_TRUE(wait([&] { return results.load() == 3; }));
  EXPECT_EQ(executions.load(), 1);
  EXPECT_EQ(configs.load(), 1);
}

TEST(ConnectorConformance, SnapshotBoundaryIsCapturedBeforeQueryAndEventsDuringQueryWin) {
  std::atomic_bool querying{false};
  std::atomic_bool queried{false};
  std::atomic_bool observed{false};
  StateTracker tracker;
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    auto parameters = registered("state-session", 2);
    parameters.mutable_registered()->add_capabilities("state.v1");
    parameters.mutable_registered()->add_capabilities("snapshot.v1");
    tracker.connected("state-session");
    (void)stream->Write(parameters);
    while (receive(stream, &request)) {
      if (request.has_event_batch()) {
        for (const auto& event : request.event_batch().events()) {
          (void)tracker.apply(event, {{"alarm", "true"}});
        }
        acknowledge(stream, request.event_batch());
      } else if (request.has_state_report()) {
        for (const auto& observation : request.state_report().observations()) {
          EXPECT_EQ(observation.cursor().source_seq(), 1);
          (void)tracker.apply(observation);
        }
        EXPECT_EQ(tracker.get("door-1").observation.values().at("alarm"), "true");
        EXPECT_EQ(tracker.get("door-1").quality, Quality::kActual);
        observed.store(true);
      }
    }
    return grpc::Status::OK;
  });
  Client client(peer.options(),
                {.snapshot = [&](const std::vector<wire::SourceCursor>& boundaries, const std::stop_token& stop) {
                  EXPECT_EQ(boundaries.at(0).source_seq(), 1);
                  querying.store(true);
                  (void)wait([&] { return queried.load() || stop.stop_requested(); });
                  wire::StateObservation snapshot;
                  snapshot.mutable_cursor()->set_source_ref("door-1");
                  snapshot.mutable_cursor()->set_source_seq(9999);
                  snapshot.set_availability(wire::DEVICE_AVAILABILITY_ONLINE);
                  *snapshot.mutable_observed_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
                  (*snapshot.mutable_values())["alarm"] = "false";
                  return std::vector{snapshot};
                }});
  ASSERT_TRUE(client.publish(alarm()));
  ASSERT_TRUE(client.start());
  ASSERT_TRUE(wait([&] { return querying.load(); }));
  auto live = client.publish(alarm(), Observation::kCurrent);
  ASSERT_TRUE(live);
  EXPECT_TRUE(live->live_observation());
  EXPECT_EQ(live->source_seq(), 2);
  ASSERT_TRUE(client.flush(std::chrono::milliseconds(5000)));
  queried.store(true);
  ASSERT_TRUE(wait([&] { return observed.load(); }));
}

TEST(ConnectorConformance, SilentPeerTimesOutAndStopCancelsBlockedRead) {
  std::atomic_int sessions{0};
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    ++sessions;
    wire::SessionRequest request;
    while (stream->Read(&request)) {
    }
    return grpc::Status::OK;
  });
  auto options = peer.options();
  options.registration_timeout = std::chrono::milliseconds(100);
  Client client(options);
  ASSERT_TRUE(client.start());
  ASSERT_TRUE(wait([&] { return sessions.load() >= 2; }));
  const auto before = std::chrono::steady_clock::now();
  client.stop();
  EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::milliseconds(1000));
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(client.start());
    client.stop();
    EXPECT_FALSE(client.diagnostics().connected);
  }
}

TEST(ConnectorConformance, UntrustedServerCannotDrainBufferAndSecretsAreNeverLogged) {
  Peer peer([](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    while (stream->Read(&request)) {
    }
    return grpc::Status::OK;
  });
  const fixture::Fixture untrusted;
  auto options = peer.options();
  options.ca_file = untrusted.options(untrusted.client, "untrusted").ca_file;
  Client client(options);
  ASSERT_TRUE(client.publish(alarm()));
  ASSERT_TRUE(client.start());
  EXPECT_FALSE(client.flush(std::chrono::milliseconds(300)));
  EXPECT_FALSE(client.diagnostics().connected);
  client.stop();
  options.private_key_file = "/missing-sensitive-key";
  auto connection = channel(options);
  ASSERT_FALSE(connection);
  EXPECT_EQ(connection.error().message().find("sensitive"), std::string::npos);
}

TEST(ConnectorConformance, HistoryAndCurrentObservationsAreDistinctAndFinalAckIsNotLost) {
  std::atomic_int events{0};
  std::atomic_int reports{0};
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    auto parameters = registered("current-session", 2);
    parameters.mutable_registered()->add_capabilities("state.v1");
    (void)stream->Write(parameters);
    while (receive(stream, &request)) {
      if (request.has_event_batch()) {
        for (const auto& event : request.event_batch().events()) {
          EXPECT_EQ(event.live_observation(), event.raw_code() == "CURRENT");
          ++events;
        }
        acknowledge(stream, request.event_batch());
      } else if (request.has_state_report()) {
        EXPECT_FALSE(request.state_report().snapshot());
        ++reports;
      }
      if (events.load() == 2 && reports.load() == 1) {
        return grpc::Status::OK;
      }
    }
    return grpc::Status::OK;
  });
  Client client(peer.options());
  wire::StateObservation observation;
  observation.mutable_cursor()->set_source_ref("door-1");
  *observation.mutable_observed_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
  observation.set_availability(wire::DEVICE_AVAILABILITY_OFFLINE);
  EXPECT_FALSE(client.report(observation));
  ASSERT_TRUE(client.start());
  ASSERT_TRUE(wait([&] { return client.diagnostics().connected; }));
  ASSERT_TRUE(client.publish(alarm()));
  auto current = alarm();
  current.set_raw_code("CURRENT");
  ASSERT_TRUE(client.publish(current, Observation::kCurrent));
  ASSERT_TRUE(client.report(observation));
  observation.mutable_observed_at()->set_nanos(-1);
  EXPECT_FALSE(client.report(observation));
  EXPECT_TRUE(client.flush(std::chrono::milliseconds(5000)));
  EXPECT_TRUE(wait([&] { return events.load() == 2 && reports.load() == 1; }));
}

TEST(ConnectorConformance, BatchAckTimeoutIsIndependentOfHeartbeats) {
  std::atomic_int sessions{0};
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    const auto attempt = ++sessions;
    (void)stream->Write(registered("ack-timeout-" + std::to_string(attempt)));
    while (receive(stream, &request)) {
      if (request.has_event_batch() && attempt > 1) {
        acknowledge(stream, request.event_batch());
      }
    }
    return grpc::Status::OK;
  });
  auto options = peer.options();
  options.acknowledgement_timeout = std::chrono::milliseconds(100);
  Client client(options);
  ASSERT_TRUE(client.publish(alarm()));
  ASSERT_TRUE(client.start());
  EXPECT_TRUE(client.flush(std::chrono::milliseconds(5000)));
  EXPECT_GE(sessions.load(), 2);
}

TEST(ConnectorConformance, LargeSnapshotUsesBoundedControlQueueWithoutDroppingSources) {
  std::atomic_int sources{0};
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    auto parameters = registered("large-snapshot");
    parameters.mutable_registered()->add_capabilities("state.v1");
    parameters.mutable_registered()->add_capabilities("snapshot.v1");
    parameters.mutable_registered()->mutable_session_timeout()->set_seconds(10);
    (void)stream->Write(parameters);
    while (receive(stream, &request)) {
      EXPECT_TRUE(request.has_state_report());
      EXPECT_LE(request.state_report().ByteSizeLong(), 65536);
      sources.fetch_add(request.state_report().observations_size());
    }
    return grpc::Status::OK;
  });
  Client client(peer.options(), {.snapshot = [](const std::vector<wire::SourceCursor>& /*boundaries*/,
                                                const std::stop_token& /*stop*/) {
                  std::vector<wire::StateObservation> observations;
                  observations.reserve(200);
                  for (int i = 0; i < 200; ++i) {
                    wire::StateObservation observation;
                    observation.mutable_cursor()->set_source_ref("source-" + std::to_string(i));
                    *observation.mutable_observed_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
                    observation.set_availability(wire::DEVICE_AVAILABILITY_ONLINE);
                    (*observation.mutable_values())["state"] = std::string(32000, 'x');
                    observations.push_back(std::move(observation));
                  }
                  return observations;
                }});
  ASSERT_TRUE(client.start());
  EXPECT_TRUE(wait([&] { return sources.load() == 200; }, std::chrono::milliseconds(10000)));
  EXPECT_TRUE(client.diagnostics().last_error.empty());
}

TEST(ConnectorConformance, RegistrationDeadlineAlsoBoundsAnIncompleteTlsHandshake) {
  Peer peer([](grpc::ServerContext* /*context*/, Stream* /*stream*/) { return grpc::Status::OK; });
  boost::asio::io_context io;
  const boost::asio::ip::tcp::acceptor silent_listener(io, {boost::asio::ip::address_v4::loopback(), 0});
  auto options = peer.options();
  options.endpoint = "127.0.0.1:" + std::to_string(silent_listener.local_endpoint().port());
  options.registration_timeout = std::chrono::milliseconds(100);
  Client client(options);
  ASSERT_TRUE(client.start());
  EXPECT_TRUE(wait([&] { return !client.diagnostics().last_error.empty(); }, std::chrono::milliseconds(1500)));
  EXPECT_FALSE(client.diagnostics().connected);
  client.stop();
}

TEST(ConnectorConformance, TenMinuteOutage) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): the test process never mutates its environment
  const auto* configured = std::getenv("PSIM_CONNECTOR_OUTAGE_SECONDS");
  const auto seconds = configured != nullptr ? std::stoi(configured) : 1;
  ASSERT_GE(seconds, 1);
  ASSERT_LE(seconds, 600);
  std::atomic_bool broken{false};
  std::atomic<std::int64_t> restore_at{std::numeric_limits<std::int64_t>::max()};
  std::mutex mutex;
  std::set<std::string> received;
  std::string unacknowledged;
  Peer peer([&](grpc::ServerContext* /*context*/, Stream* stream) {
    wire::SessionRequest request;
    if (!stream->Read(&request)) {
      return grpc::Status::OK;
    }
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    if (broken.load() && now < restore_at.load()) {
      return grpc::Status(grpc::StatusCode::UNAVAILABLE, "equipment link unavailable");
    }
    const auto session_id = "outage-" + detail::uuid7();
    (void)stream->Write(registered(session_id, 2));
    while (receive(stream, &request)) {
      if (!request.has_event_batch()) {
        continue;
      }
      {
        const std::scoped_lock lock(mutex);
        for (const auto& event : request.event_batch().events()) {
          received.insert(event.event_id());
          if (broken.load() && event.observation_session_id() != session_id) {
            EXPECT_FALSE(event.live_observation());
          }
        }
      }
      if (!broken.exchange(true)) {
        unacknowledged = request.event_batch().events(0).event_id();
        restore_at.store((std::chrono::steady_clock::now() + std::chrono::seconds(seconds)).time_since_epoch().count());
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "lost acknowledgement before outage");
      }
      acknowledge(stream, request.event_batch());
    }
    return grpc::Status::OK;
  });
  auto options = peer.options();
  std::set<std::string> expected;
  auto client = std::make_unique<Client>(options);
  auto first = client->publish(alarm());
  ASSERT_TRUE(first);
  expected.insert(first->event_id());
  ASSERT_TRUE(client->start());
  ASSERT_TRUE(wait([&] { return broken.load(); }));
  for (int second = 0; second < seconds; ++second) {
    auto event = client->publish(alarm());
    ASSERT_TRUE(event);
    expected.insert(event->event_id());
    if (second == seconds / 2) {
      client.reset();
      client = std::make_unique<Client>(options);
      ASSERT_TRUE(client->start());
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  EXPECT_TRUE(client->flush(std::chrono::milliseconds(10000)));
  client->stop();
  const std::scoped_lock lock(mutex);
  EXPECT_EQ(received, expected);
  EXPECT_TRUE(received.contains(unacknowledged));
  EXPECT_EQ(client->diagnostics().pending_events, 0);
}
}  // namespace
}  // namespace psim::platform::connector
