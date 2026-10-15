#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <google/protobuf/util/time_util.h>
#include <grpc/grpc_security_constants.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/status.h>
#include <psim/common/v1/envelope.pb.h>
#include <psim/connector/v1/connector.pb.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <expected>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "psim/connector/v1/connector.grpc.pb.h"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/secret.hpp"
#include "psim/platform/stream/grpc_process.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::connector_gateway {
namespace p = platform;
namespace asio = boost::asio;
using Request = connector::v1::SessionRequest;
using Response = connector::v1::SessionResponse;

namespace {
std::string pem(const std::string& path) {
  std::ifstream file(path);
  std::string result((std::istreambuf_iterator<char>(file)), {});
  if (!file || result.empty() || result.size() > 1048576) {
    throw std::runtime_error("cannot read TLS material");
  }
  return result;
}
}  // namespace
class Gateway;

class Session final : public grpc::ServerBidiReactor<Request, Response> {
 public:
  Session(Gateway* gateway, bool admitted);
  void OnReadDone(bool ok) override;
  void OnWriteDone(bool ok) override;

  void OnCancel() override { cancelled_.store(true); }

  void OnDone() override;

 private:
  asio::awaitable<void> dispatch();
  Gateway* gateway_;
  Request request_;
  Response response_;
  bool admitted_, registered_ = false;
  std::atomic_bool cancelled_{false};
};

class Gateway final : public p::stream::Worker, public connector::v1::ConnectorGatewayService::CallbackService {
 public:
  explicit Gateway(const p::runtime::Context& context) : p::stream::Worker(context, "ingest", "") {}

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* /*message*/, p::kafka::Output* /*output*/) override {
    co_return p::Result<void>{};
  }

  grpc::ServerBidiReactor<Request, Response>* Session(grpc::CallbackServerContext* context) override {
    bool allowed = false;
    for (const auto& value : context->auth_context()->FindPropertyValues("x509_subject_alternative_name")) {
      if (std::string_view(value.data(), value.size()) == "psim://connector/22222222-2222-4222-8222-222222222222") {
        allowed = true;
      }
    }
    const bool admitted = allowed && active_.fetch_add(1) < 8;
    if (allowed && !admitted) {
      --active_;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): gRPC transfers the reactor to OnDone
    return new connector_gateway::Session(
        this, admitted);  // NOLINT(cppcoreguidelines-owning-memory): reactor owns itself until OnDone
  }

  const asio::any_io_executor& shard() const { return executor(); }

  void released() { --active_; }

  asio::awaitable<p::Result<void>> initialize() override {
    auto result = co_await schema("psim.ingest.raw.v1");
    if (!result) {
      co_return result;
    }
    auto started = co_await context().blocking().run([this]() -> p::Result<void> {
      const auto directory = context().config().get<std::string>("settings.tls_directory");
      auto key = p::security::read_secret(directory + "/server.key");
      if (!key) {
        return std::unexpected(key.error());
      }
      auto provider = std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
      const auto roots = provider->UpdateRoot(pem(directory + "/ca.pem"));
      const auto identity =
          provider->UpdateIdentityKeyCertPair(std::vector<grpc::experimental::IdentityKeyOrSignerCertPair>{
              {std::string(key->view()), pem(directory + "/server.pem")}});
      auto options = grpc::experimental::TlsServerCredentialsOptions::Create(provider);
      if (!roots.ok() || !identity.ok() || !options.ok()) {
        return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid connector TLS material");
      }
      options->set_root_certificate_provider(provider);
      options->set_min_tls_version(TLS1_3);
      options->set_cert_request_type(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
      grpc::ServerBuilder builder;
      builder.SetMaxReceiveMessageSize(256 * 1024);
      builder.RegisterService(this);
      builder.AddListeningPort("0.0.0.0:" + std::to_string(context().config().get<int>("settings.listen_port")),
                               grpc::experimental::TlsServerCredentials(*options));
      server_ = builder.BuildAndStart();
      if (!server_) {
        return p::fail(p::ErrorCode::kCommonUnavailable, "connector listener failed");
      }
      return {};
    });
    co_return started;
  }

  asio::awaitable<p::Result<Response>> process(Request request, bool* registered) {
    Response response;
    if (!*registered) {
      if (!request.has_register_()
          || std::ranges::find(request.register_().protocol_versions(), "1.0")
                 == request.register_().protocol_versions().end()) {
        co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "Register with protocol 1.0 must be first");
      }
      *registered = true;
      auto* value = response.mutable_registered();
      value->set_session_id(p::stream::uuid7());
      value->set_connector_id("22222222-2222-4222-8222-222222222222");
      value->set_protocol_version("1.0");
      value->set_initial_credits(1);
      value->set_max_batch_events(1);
      value->set_max_batch_bytes(65536);
      value->mutable_heartbeat_interval()->set_seconds(5);
      value->mutable_session_timeout()->set_seconds(30);
      co_return response;
    }
    if (request.has_heartbeat()) {
      *response.mutable_heartbeat()->mutable_sent_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
      co_return response;
    }
    if (!request.has_event_batch() || request.event_batch().events_size() != 1
        || request.event_batch().ByteSizeLong() > 65536) {
      co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "fixture session accepts one event per batch");
    }
    const auto& input = request.event_batch().events(0);
    if (!p::stream::uuid(input.event_id()) || input.source_ref().empty() || input.source_ref().size() > 256
        || input.source_seq() == 0 || !input.has_occurred_at() || input.raw_code().empty()
        || input.raw_code().size() > 128 || input.attributes_size() > 64 || input.traceparent().size() > 55) {
      co_return p::fail(p::ErrorCode::kIngestInvalidEvent, "invalid connector event");
    }
    const auto now = google::protobuf::util::TimeUtil::GetCurrentTime();
    if (input.occurred_at().seconds() < now.seconds() - 300 || input.occurred_at().seconds() > now.seconds() + 300
        || input.occurred_at().nanos() < 0 || input.occurred_at().nanos() >= 1000000000) {
      co_return p::fail(p::ErrorCode::kIngestInvalidEvent, "fixture event time must be within five minutes");
    }
    auto headers = p::kafka::Headers{{"traceparent", input.traceparent()}};
    auto trace = p::stream::span(context(), "accept connector event", headers);
    ingest::v1::RawEventRecord event;
    auto* envelope = event.mutable_envelope();
    envelope->set_message_id(input.event_id());
    envelope->set_tenant_id("11111111-1111-4111-8111-111111111111");
    *envelope->mutable_occurred_at() = input.occurred_at();
    *envelope->mutable_received_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
    *envelope->mutable_produced_at() = envelope->received_at();
    envelope->set_sequence(input.source_seq());
    envelope->set_correlation_id(input.event_id());
    envelope->set_schema_version("1");
    envelope->mutable_actor()->set_kind(common::v1::ACTOR_KIND_CONNECTOR);
    envelope->mutable_actor()->set_id("22222222-2222-4222-8222-222222222222");
    auto* raw = event.mutable_raw_event();
    raw->set_connector_id(envelope->actor().id());
    raw->set_source_ref(input.source_ref());
    raw->set_source_id(
        p::stream::uuid5("728d47ed-1c69-5809-a2df-6e8b98e48ca6", {raw->connector_id(), raw->source_ref()}));
    raw->set_source_epoch(input.source_epoch());
    raw->set_raw_code(input.raw_code());
    *raw->mutable_attributes() = input.attributes();
    p::kafka::inject(*trace, headers);
    auto delivered =
        co_await producer()->send(record("psim.ingest.raw.v1", raw->source_id(), event, std::move(headers)));
    trace->End();
    if (!delivered) {
      co_return std::unexpected(delivered.error());
    }
    response.mutable_batch_ack()->set_batch_seq(request.event_batch().batch_seq());
    response.mutable_batch_ack()->set_accepted(1);
    co_return response;
  }

  asio::awaitable<void> shutdown() override {
    (void)co_await context().blocking().run([this] {
      if (server_) {
        server_->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(10));
        server_->Wait();
        server_.reset();
      }
    });
  }

 private:
  std::unique_ptr<grpc::Server> server_;
  std::atomic_uint active_{0};
};

Session::Session(Gateway* gateway, bool admitted) : gateway_(gateway), admitted_(admitted) {
  if (!admitted_) {
    Finish(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "connector identity or capacity denied"));
  } else {
    StartRead(&request_);
  }
}

void Session::OnReadDone(bool ok) {
  if (!ok) {
    Finish(grpc::Status::OK);
    return;
  }
  asio::co_spawn(gateway_->shard(), dispatch(), asio::detached);
}

asio::awaitable<void> Session::dispatch() {
  auto result = co_await gateway_->process(request_, &registered_);
  if (cancelled_.load()) {
    Finish(grpc::Status(grpc::StatusCode::CANCELLED, "session cancelled"));
  } else if (!result) {
    Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, std::string(result.error().name())));
  } else {
    response_ = std::move(*result);
    StartWrite(&response_);
  }
}

void Session::OnWriteDone(bool ok) {
  if (ok && response_.has_batch_ack()) {
    response_.Clear();
    response_.mutable_credits()->set_grant(1);
    StartWrite(&response_);
  } else if (ok) {
    request_.Clear();
    StartRead(&request_);
  } else {
    Finish(grpc::Status(grpc::StatusCode::CANCELLED, "session closed"));
  }
}

void Session::OnDone() {
  if (admitted_) {
    gateway_->released();
  }
  delete this;
}  // NOLINT(cppcoreguidelines-owning-memory): gRPC final callback
}  // namespace psim::connector_gateway

int main(int argc, char** argv) {
  const psim::platform::stream::GrpcProcess grpc;
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "connector-gateway",
                                       .settings_schema = std::string(psim::platform::stream::kSettings),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::connector_gateway::Gateway>(context));
                                         return result;
                                       }});
}
