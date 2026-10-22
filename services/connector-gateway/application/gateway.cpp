#include "gateway.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/time_util.h>
#include <google/rpc/error_details.pb.h>
#include <google/rpc/status.pb.h>
#include <grpc/grpc_security_constants.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/status.h>
#include <nlohmann/json.hpp>
#include <psim/connector/v1/administration.grpc.pb.h>
#include <psim/connector/v1/administration.pb.h>
#include <psim/connector/v1/connector.grpc.pb.h>
#include <psim/connector/v1/connector.pb.h>
#include <psim/ingest/v1/raw_event.pb.h>
#include <psim/ingest/v1/state.pb.h>
#include <psim/response/v1/command.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "certificate.hpp"
#include "protocol.hpp"
#include "psim/platform/connector/state.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/redis/patterns.hpp"
#include "psim/platform/redis/pool.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/secret.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "registry.hpp"

namespace psim::connector_gateway {
namespace p = platform;
namespace asio = boost::asio;
using Request = connector::v1::SessionRequest;
using Response = connector::v1::SessionResponse;
using Util = google::protobuf::util::TimeUtil;
using Steady = std::chrono::steady_clock;

namespace {
constexpr int kHeartbeat = 5;
constexpr int kTimeout = 15;
constexpr std::string_view kQuota =
    "local c=tonumber(redis.call('GET',KEYS[1]) or '0'); "
    "if c+tonumber(ARGV[1])>tonumber(ARGV[2]) then return redis.call('PTTL',KEYS[1]) end; "
    "c=redis.call('INCRBY',KEYS[1],ARGV[1]); if c==tonumber(ARGV[1]) then redis.call('PEXPIRE',KEYS[1],1000) end; "
    "return 0";

std::string pem(const std::string& path) {
  std::ifstream file(path);
  std::string value((std::istreambuf_iterator<char>(file)), {});
  if (!file || value.empty() || value.size() > 1048576) {
    throw std::runtime_error("cannot read TLS material");
  }
  return value;
}

grpc::Status status(const p::Error& error) {
  const auto name = std::string(error.name());
  const auto catalog = p::info(error.code()).grpc_status;
  grpc::StatusCode code = grpc::StatusCode::INTERNAL;
  for (const auto& [label, value] :
       {std::pair<std::string_view, grpc::StatusCode>{"CANCELLED", grpc::StatusCode::CANCELLED},
        {"UNKNOWN", grpc::StatusCode::UNKNOWN},
        {"INVALID_ARGUMENT", grpc::StatusCode::INVALID_ARGUMENT},
        {"DEADLINE_EXCEEDED", grpc::StatusCode::DEADLINE_EXCEEDED},
        {"NOT_FOUND", grpc::StatusCode::NOT_FOUND},
        {"ALREADY_EXISTS", grpc::StatusCode::ALREADY_EXISTS},
        {"PERMISSION_DENIED", grpc::StatusCode::PERMISSION_DENIED},
        {"RESOURCE_EXHAUSTED", grpc::StatusCode::RESOURCE_EXHAUSTED},
        {"FAILED_PRECONDITION", grpc::StatusCode::FAILED_PRECONDITION},
        {"ABORTED", grpc::StatusCode::ABORTED},
        {"OUT_OF_RANGE", grpc::StatusCode::OUT_OF_RANGE},
        {"UNIMPLEMENTED", grpc::StatusCode::UNIMPLEMENTED},
        {"INTERNAL", grpc::StatusCode::INTERNAL},
        {"UNAVAILABLE", grpc::StatusCode::UNAVAILABLE},
        {"DATA_LOSS", grpc::StatusCode::DATA_LOSS},
        {"UNAUTHENTICATED", grpc::StatusCode::UNAUTHENTICATED}}) {
    if (catalog == label) {
      code = value;
      break;
    }
  }
  google::rpc::ErrorInfo info;
  info.set_reason(name);
  info.set_domain("psim");
  (*info.mutable_metadata())["retryable"] = error.retryable() ? "true" : "false";
  google::rpc::Status details;
  details.set_code(static_cast<int>(code));
  details.set_message(name);
  details.add_details()->PackFrom(info);
  return {code, name, details.SerializeAsString()};
}
}  // namespace

std::string settings_schema() {
  auto schema = nlohmann::json::parse(p::stream::kSettings);
  auto& fields = schema["properties"];
  for (const auto& name : {"redis_host", "redis_ca_file", "redis_certificate_file", "redis_key_file"}) {
    fields[name] = {{"type", "string"}, {"default", name == std::string_view("redis_host") ? "valkey" : ""}};
  }
  fields["redis_password_file"] = {{"type", "string"}, {"x-psim-secret", true}};
  fields["max_sessions"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 10000}, {"default", 256}};
  return schema.dump();
}

struct SessionState {
  SessionState(Gateway* owner, grpc::CallbackServerContext* call, p::Result<Certificate> peer)
      : gateway(owner),
        context(call),
        identity(std::move(peer)),
        id(p::stream::uuid7()),
        seen(Steady::now()),
        lease_until(seen),
        renew_at(seen) {}

  Gateway* gateway;
  grpc::CallbackServerContext* context;
  grpc::ServerBidiReactor<Request, Response>* transport = nullptr;
  p::Result<Certificate> identity;
  std::string id, connector, tenant;
  Request request;
  std::deque<Response> outgoing;
  p::connector::StateTracker tracker;
  std::set<std::string> sources;

  struct Command {
    std::string fingerprint;
    std::int64_t deadline;
  };

  std::map<std::string, Command> commands;
  Steady::time_point seen, lease_until, renew_at;
  std::uint64_t batch = 0;
  std::uint32_t max_batch = 0, rate = 0, credits = 0;
  bool registered = false, reading = false, writing = false, processing = false, finishing = false, stopped = false,
       renewing = false, claimed = false;
  bool state_capability = false, snapshot_capability = false, command_capability = false;
  grpc::Status final_status;
  std::string reason = "disconnected";

  void finish() {
    if (transport != nullptr && stopped && !reading && !writing && !processing && !finishing) {
      finishing = true;
      transport->Finish(final_status);
    }
  }

  void stop(grpc::Status value, std::string why) {
    if (stopped) {
      return;
    }
    stopped = true;
    final_status = std::move(value);
    reason = std::move(why);
    if (transport != nullptr && (reading || writing || processing)) {
      context->TryCancel();
    }
    finish();
  }

  void send(Response value) {
    if (stopped || transport == nullptr) {
      return;
    }
    if (outgoing.size() >= 64) {
      stop(grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "connector output queue full"), "backpressure");
      return;
    }
    if (value.has_credits()) {
      credits += value.credits().grant();
    }
    outgoing.push_back(std::move(value));
    if (!writing) {
      writing = true;
      transport->StartWrite(&outgoing.front());
    }
  }

  void read() {
    if (stopped || transport == nullptr) {
      finish();
      return;
    }
    request.Clear();
    reading = true;
    transport->StartRead(&request);
  }
};

class Reactor final : public grpc::ServerBidiReactor<Request, Response> {
 public:
  Reactor(Gateway* gateway, grpc::CallbackServerContext* context, p::Result<Certificate> peer)
      : state_(std::make_shared<SessionState>(gateway, context, std::move(peer))) {
    state_->transport = this;
    asio::post(gateway->shard(), [state = state_] { state->gateway->add(state); });
  }

  void OnReadDone(bool ok) override {
    asio::post(state_->gateway->shard(), [state = state_, ok] {
      state->reading = false;
      if (!ok || state->stopped) {
        state->stop(grpc::Status::OK, "disconnected");
        state->finish();
        return;
      }
      if (state->request.has_heartbeat()) {
        state->seen = Steady::now();
      }
      state->processing = true;
      asio::co_spawn(state->gateway->shard(), state->gateway->dispatch(state), asio::detached);
    });
  }

  void OnWriteDone(bool ok) override {
    asio::post(state_->gateway->shard(), [state = state_, ok] {
      state->writing = false;
      if (!state->outgoing.empty()) {
        state->outgoing.pop_front();
      }
      if (!ok) {
        state->stop(grpc::Status(grpc::StatusCode::CANCELLED, "connector write closed"), "disconnected");
      } else if (!state->outgoing.empty() && !state->stopped) {
        state->writing = true;
        state->transport->StartWrite(&state->outgoing.front());
      }
      state->finish();
    });
  }

  void OnCancel() override {
    asio::post(state_->gateway->shard(), [state = state_] {
      state->stop(grpc::Status(grpc::StatusCode::CANCELLED, "connector cancelled"), "disconnected");
    });
  }

  void OnDone() override {
    asio::post(state_->gateway->shard(), [this, state = state_] {
      state->transport = nullptr;
      state->context = nullptr;
      state->gateway->removed(state);
      delete this;
    });
  }  // NOLINT(cppcoreguidelines-owning-memory): gRPC reactor is released only after its final callback
 private:
  std::shared_ptr<SessionState> state_;
};

struct DeliveryBatch {
  explicit DeliveryBatch(const asio::any_io_executor& executor, std::size_t count) : wake(executor), remaining(count) {}

  asio::steady_timer wake;
  std::size_t remaining;
  std::optional<p::Error> error;
};

Gateway::Gateway(const p::runtime::Context& context)
    : p::stream::Worker(context, "ingest", ""),
      tick_(executor()),
      refreshed_(executor()),
      published_(executor()),
      idle_(executor()),
      boot_(p::stream::uuid7()) {
  auto& registry = context.metrics().registry();
  auto counter = [&registry](const std::string& name, const std::string& help) {
    return &prometheus::BuildCounter().Name(name).Help(help).Register(registry).Add({});
  };
  accepted_ = counter("psim_gateway_events_accepted_total", "Connector events durably acknowledged by Kafka");
  rejected_ = counter("psim_gateway_events_rejected_total", "Permanently rejected connector events");
  commands_sent_ = counter("psim_gateway_commands_sent_total", "Commands delivered to the owning connector session");
  expired_ = counter("psim_gateway_heartbeat_timeouts_total", "Sessions closed after three missed heartbeat intervals");
  denied_ = counter("psim_gateway_admissions_denied_total", "Rejected connector admissions");
  connected_ = &prometheus::BuildGauge()
                    .Name("psim_gateway_sessions")
                    .Help("Open connector transports")
                    .Register(registry)
                    .Add({});
}

grpc::ServerBidiReactor<Request, Response>* Gateway::Session(grpc::CallbackServerContext* context) {
  auto peer = certificate(*context->auth_context(), ca_file_);
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): gRPC transfers ownership to OnDone
  return new Reactor(this, context, std::move(peer));
}

void Gateway::add(std::shared_ptr<SessionState> session) {
  if (stopping_ || sessions_.size() >= static_cast<std::size_t>(context().config().get<int>("settings.max_sessions"))
      || !session->identity || !healthy_.load()) {
    denied_->Increment();
    session->stop(grpc::Status(grpc::StatusCode::UNAVAILABLE, "connector admission unavailable"), "admission_denied");
    return;
  }
  constexpr std::string_view kPrefix = "psim://connector/";
  if (!session->identity->identity.starts_with(kPrefix)) {
    denied_->Increment();
    session->stop(grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "connector identity required"), "admission_denied");
    return;
  }
  session->connector = session->identity->identity.substr(kPrefix.size());
  sessions_.emplace(session->id, session);
  connected_->Set(static_cast<double>(sessions_.size()));
  session->read();
}

void Gateway::removed(std::shared_ptr<SessionState> session) {
  sessions_.erase(session->id);
  connected_->Set(static_cast<double>(sessions_.size()));
  if (session->claimed && registry_ && redis_) {
    ++releasing_;
    asio::co_spawn(executor(), release(std::move(session)), asio::detached);
  }
  idle_.cancel();
}

asio::awaitable<void> Gateway::release(std::shared_ptr<SessionState> session) {
  const auto saved = co_await registry_->disconnected(session->id, session->reason);
  if (!saved) {
    context().logger().warn("connector offline status deferred");
  }
  p::redis::SessionRegistry leases(redis_.get(), {"connector-gateway", session->tenant});
  const auto released = co_await leases.release(session->connector, {session->id, boot_});
  if (!released) {
    context().logger().warn("connector lease release deferred");
  }
  --releasing_;
  idle_.cancel();
}

bool Gateway::allowed(const SessionState& session) const {
  const auto found = snapshot_.registrations.find(session.connector);
  return session.identity && found != snapshot_.registrations.end()
         && authorized(found->second, *session.identity, context().clock().wall())
         && Steady::now() - verified_ < std::chrono::seconds(1)
         && (!session.registered || Steady::now() < session.lease_until);
}

asio::awaitable<p::Result<void>> Gateway::initialize() {
  const auto& config = context().config();
  ca_file_ = config.get<std::string>("settings.tls_directory") + "/ca.pem";
  for (const auto* topic :
       {"psim.ingest.raw.v1", "psim.ingest.state.v1", "psim.commands.results.v1", "psim.audit.v1", "psim.catalog.v1"}) {
    auto value = co_await schema(topic);
    if (!value) {
      co_return value;
    }
  }
  auto database = co_await p::stream::Worker::database();
  if (!database) {
    co_return std::unexpected(database.error());
  }
  database_ = std::move(*database);
  registry_ = std::make_unique<Registry>(
      database_.get(), [this](std::string topic, std::string key, const google::protobuf::Message& value) {
        return record(std::move(topic), std::move(key), value, {});
      });
  auto initialized = co_await registry_->initialize();
  if (!initialized) {
    co_return initialized;
  }
  auto snapshot = co_await registry_->snapshot();
  if (!snapshot) {
    co_return std::unexpected(snapshot.error());
  }
  snapshot_ = std::move(*snapshot);
  verified_ = Steady::now();
  p::redis::PoolOptions options;
  options.connection.endpoint.host = config.get<std::string>("settings.redis_host");
  options.connection.tls.enabled = !config.get<bool>("settings.allow_plaintext");
  options.connection.tls.ca_file = config.get<std::string>("settings.redis_ca_file");
  options.connection.tls.certificate_file = config.get<std::string>("settings.redis_certificate_file");
  options.connection.tls.private_key_file = config.get<std::string>("settings.redis_key_file");
  if (const auto password = config.secret("settings.redis_password_file")) {
    options.connection.credentials.password = std::string(*password);
  }
  options.connection.command_timeout = std::chrono::milliseconds(500);
  options.connection.connect_timeout = std::chrono::milliseconds(500);
  redis_ = std::make_unique<p::redis::Pool>(std::move(options));
  auto connection = co_await redis_->acquire();
  if (!connection) {
    co_return std::unexpected(connection.error());
  }
  auto verifier =
      p::security::JwksCache::create({.jwt = {.issuer = config.get<std::string>("settings.issuer"), .audience = "psim"},
                                      .url = config.get<std::string>("settings.jwks_url"),
                                      .http = {.allow_plaintext = config.get<bool>("settings.allow_plaintext")}});
  if (!verifier) {
    co_return std::unexpected(verifier.error());
  }
  verifier_ = std::move(*verifier);
  auto keys = co_await verifier_->refresh();
  if (!keys) {
    co_return keys;
  }
  auto commands = p::kafka::Consumer::create(executor(),
                                             {.client = kafka_options("gateway-commands-" + boot_),
                                              .group_id = "gateway-commands-" + boot_,
                                              .topics = {"psim.commands.v1"},
                                              .group_protocol = "classic",
                                              .auto_offset_reset = "latest"},
                                             {.blocking = &context().blocking(), .logger = &context().logger()});
  if (!commands) {
    co_return std::unexpected(commands.error());
  }
  command_consumer_ = std::move(*commands);
  const auto deadline = Steady::now() + std::chrono::seconds(30);
  while (command_consumer_->assignment().empty()) {
    auto batch = co_await command_consumer_->poll(32, std::chrono::milliseconds(100));
    if (!batch) {
      co_return std::unexpected(batch.error());
    }
    if (Steady::now() >= deadline) {
      co_return p::fail(p::ErrorCode::kCommonDeadlineExceeded, "command reader assignment timed out");
    }
  }
  relay_ = std::make_unique<p::pg::OutboxRelay>(database_.get(), registry_->messages(),
                                                p::pg::kafka_publisher(producer()), 128);
  auto acknowledged = co_await registry_->acknowledge(boot_, snapshot_.revision);
  if (!acknowledged) {
    co_return acknowledged;
  }
  background_ = 3;
  healthy_.store(true);
  asio::co_spawn(executor(), refresh(), asio::detached);
  asio::co_spawn(executor(), publish(), asio::detached);
  asio::co_spawn(executor(), this->commands(), asio::detached);
  timer();
  co_return co_await open_listener();
}

asio::awaitable<p::Result<void>> Gateway::open_listener() {
  co_return co_await context().blocking().run([this]() -> p::Result<void> {
    const auto directory = context().config().get<std::string>("settings.tls_directory");
    auto key = p::security::read_secret(directory + "/server.key");
    if (!key) {
      return std::unexpected(key.error());
    }
    auto provider = std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    auto roots = provider->UpdateRoot(pem(ca_file_));
    auto identity = provider->UpdateIdentityKeyCertPair(std::vector<grpc::experimental::IdentityKeyOrSignerCertPair>{
        {std::string(key->view()), pem(directory + "/server.pem")}});
    auto options = grpc::experimental::TlsServerCredentialsOptions::Create(provider);
    if (!roots.ok() || !identity.ok() || !options.ok()) {
      return p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid connector TLS material");
    }
    options->set_root_certificate_provider(provider);
    options->set_min_tls_version(TLS1_3);
    options->set_cert_request_type(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    grpc::ServerBuilder builder;
    builder.SetMaxReceiveMessageSize(1048576 + 65536);
    builder.RegisterService(static_cast<connector::v1::ConnectorGatewayService::CallbackService*>(this));
    builder.RegisterService(static_cast<connector::v1::ConnectorAdministrationService::Service*>(this));
    builder.AddListeningPort("0.0.0.0:" + std::to_string(context().config().get<int>("settings.listen_port")),
                             grpc::experimental::TlsServerCredentials(*options));
    server_ = builder.BuildAndStart();
    if (!server_) {
      return p::fail(p::ErrorCode::kCommonUnavailable, "connector listener failed");
    }
    return {};
  });
}

asio::awaitable<p::Result<void>> Gateway::handle(const p::kafka::Message* /*message*/, p::kafka::Output* /*output*/) {
  co_return p::Result<void>{};
}

void Gateway::timer() {
  if (stopping_) {
    return;
  }
  tick_.expires_after(std::chrono::milliseconds(100));
  tick_.async_wait([this](const boost::system::error_code& error) {
    if (error || stopping_) {
      return;
    }
    const auto now = Steady::now();
    for (const auto& [id, session] : sessions_) {
      if (session->stopped) {
        continue;
      }
      if (now - session->seen >= std::chrono::seconds(session->registered ? kTimeout : 5)) {
        expired_->Increment();
        session->stop(grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED, "heartbeat timeout"), "heartbeat_timeout");
      } else if (!allowed(*session)) {
        session->stop(grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "connector trust or lease expired"),
                      "trust_revoked");
      } else if (session->registered && now >= session->renew_at && !session->renewing) {
        session->renewing = true;
        ++releasing_;
        asio::co_spawn(executor(), renew(session), asio::detached);
      }
    }
    timer();
  });
}

asio::awaitable<void> Gateway::refresh() {
  while (!stopping_) {
    auto fresh = co_await registry_->snapshot();
    if (fresh) {
      snapshot_ = std::move(*fresh);
      verified_ = Steady::now();
      bool closing = false;
      for (const auto& [id, session] : sessions_) {
        if (!allowed(*session)) {
          closing = true;
          session->stop(grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "connector registration revoked"),
                        "trust_revoked");
        }
      }
      if (!closing) {
        auto acknowledged = co_await registry_->acknowledge(boot_, snapshot_.revision);
        healthy_.store(acknowledged.has_value() && command_healthy_);
      }
      auto expired = co_await registry_->expire();
      if (!expired) {
        healthy_.store(false);
      }
    } else {
      healthy_.store(false);
      context().logger().warn("connector registry refresh deferred");
    }
    refreshed_.expires_after(std::chrono::milliseconds(200));
    co_await refreshed_.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  --background_;
  idle_.cancel();
}

asio::awaitable<void> Gateway::publish() {
  while (!stopping_) {
    auto result = co_await relay_->drain_once();
    if (!result) {
      context().logger().warn("connector status or audit publication deferred");
    }
    published_.expires_after(std::chrono::milliseconds(result ? 20 : 500));
    co_await published_.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  --background_;
  idle_.cancel();
}

asio::awaitable<void> Gateway::renew(std::shared_ptr<SessionState> session) {
  const auto before = Steady::now();
  p::redis::SessionRegistry leases(redis_.get(), {"connector-gateway", session->tenant});
  const auto remaining = std::max(
      std::chrono::duration_cast<std::chrono::milliseconds>(session->seen + std::chrono::seconds(kTimeout) - before),
      std::chrono::milliseconds(1));
  auto renewed = co_await leases.renew(session->connector, {session->id, boot_}, remaining);
  p::Result<bool> saved = false;
  if (renewed && *renewed && !session->stopped) {
    saved = co_await registry_->renew(session->id,
                                      static_cast<int>(std::chrono::ceil<std::chrono::seconds>(remaining).count()));
  }
  if (!renewed || !*renewed || !saved || !*saved) {
    session->stop(grpc::Status(grpc::StatusCode::UNAVAILABLE, "connector lease renewal failed"), "lease_lost");
  } else {
    session->lease_until = before + remaining;
    session->renew_at = before + std::chrono::seconds(kHeartbeat);
    Response heartbeat;
    *heartbeat.mutable_heartbeat()->mutable_sent_at() = Util::GetCurrentTime();
    session->send(std::move(heartbeat));
  }
  session->renewing = false;
  --releasing_;
  idle_.cancel();
}

asio::awaitable<void> Gateway::shutdown() {
  stopping_ = true;
  healthy_.store(false);
  tick_.cancel();
  refreshed_.cancel();
  published_.cancel();
  for (const auto& [id, session] : sessions_) {
    session->stop(grpc::Status(grpc::StatusCode::UNAVAILABLE, "gateway draining"), "draining");
  }
  if (server_) {
    (void)co_await context().blocking().run([this] {
      server_->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(15));
      server_->Wait();
    });
  }
  while (background_ != 0 || !sessions_.empty() || releasing_ != 0) {
    idle_.expires_after(std::chrono::milliseconds(20));
    co_await idle_.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  server_.reset();
  if (command_consumer_) {
    co_await command_consumer_->close();
  }
  command_consumer_.reset();
  relay_.reset();
  registry_.reset();
  verifier_.reset();
  if (redis_) {
    redis_->close();
    redis_.reset();
  }
  if (database_) {
    database_->close();
    database_.reset();
  }
}

asio::awaitable<p::Result<void>> Gateway::register_session(std::shared_ptr<SessionState> session) {
  const auto& input = session->request.register_();
  const auto version = protocol(input);
  if (version.empty() || input.connector_version().size() > 128 || input.instance_id().size() > 128
      || input.capabilities_size() > 16 || input.protocol_versions_size() > 8 || !allowed(*session)) {
    co_return p::fail(p::ErrorCode::kAuthUnauthenticated, "registered connector and supported protocol required");
  }
  const auto registration = snapshot_.registrations.at(session->connector);
  session->tenant = registration.tenant;
  session->max_batch = std::min(registration.connector.limits().max_batch_events(),
                                registration.connector.limits().max_events_per_second());
  session->rate = registration.connector.limits().max_events_per_second();
  session->max_batch = std::min(session->max_batch, 1000U);
  session->credits = session->max_batch;
  const auto before = Steady::now();
  p::redis::SessionRegistry leases(redis_.get(), {"connector-gateway", session->tenant});
  auto claimed = co_await leases.claim(session->connector, {session->id, boot_}, std::chrono::seconds(kTimeout));
  if (!claimed) {
    co_return std::unexpected(claimed.error());
  }
  if (!*claimed) {
    co_return p::fail(p::ErrorCode::kCommonUnavailable, "connector already owns a live session");
  }
  session->claimed = true;
  auto saved = co_await registry_->connected(registration, *session->identity, session->id, kTimeout);
  if (!saved) {
    co_return saved;
  }
  session->lease_until = before + std::chrono::seconds(kTimeout);
  session->renew_at = before + std::chrono::seconds(kHeartbeat);
  if (session->stopped || !allowed(*session)) {
    co_return p::fail(p::ErrorCode::kAuthUnauthenticated, "registration changed before session activation");
  }
  session->registered = true;
  session->tracker.connected(session->id);
  session->state_capability = version == "1.1" && supports(input, "state.v1");
  session->snapshot_capability = session->state_capability && supports(input, "snapshot.v1");
  session->command_capability = version == "1.0" || supports(input, "commands.v1");
  Response response;
  auto* value = response.mutable_registered();
  value->set_session_id(session->id);
  value->set_connector_id(session->connector);
  value->set_protocol_version(version);
  value->set_initial_credits(session->max_batch);
  value->set_max_batch_events(session->max_batch);
  value->set_max_batch_bytes(1048576);
  value->mutable_heartbeat_interval()->set_seconds(kHeartbeat);
  value->mutable_session_timeout()->set_seconds(kTimeout);
  if (session->state_capability) {
    value->add_capabilities("state.v1");
  }
  if (session->snapshot_capability) {
    value->add_capabilities("snapshot.v1");
  }
  if (session->command_capability && version == "1.1") {
    value->add_capabilities("commands.v1");
  }
  session->send(std::move(response));
  co_return p::Result<void>{};
}

asio::awaitable<void> Gateway::dispatch(std::shared_ptr<SessionState> session) {
  p::Result<void> result;
  try {
    if (session->stopped || !allowed(*session)) {
      result = p::fail(p::ErrorCode::kAuthUnauthenticated, "connector no longer authorized");
    } else if (!session->registered) {
      if (!session->request.has_register_()) {
        result = p::fail(p::ErrorCode::kCommonInvalidArgument, "Register must be first");
      } else {
        result = co_await register_session(session);
      }
    } else if (session->request.has_heartbeat()) {
      if (!session->request.heartbeat().has_sent_at()
          || !Util::IsTimestampValid(session->request.heartbeat().sent_at())) {
        result = p::fail(p::ErrorCode::kCommonInvalidArgument, "invalid heartbeat timestamp");
      } else {
        Response response;
        *response.mutable_heartbeat()->mutable_sent_at() = Util::GetCurrentTime();
        session->send(std::move(response));
      }
    } else if (session->request.has_event_batch()) {
      result = co_await events(session);
    } else if (session->request.has_state_report()) {
      result = co_await state(session);
    } else if (session->request.has_command_ack() || session->request.has_command_result()) {
      result = co_await this->result(session);
    } else if (session->request.has_goodbye()) {
      session->stop(grpc::Status::OK, "goodbye");
    } else {
      result = p::fail(p::ErrorCode::kCommonInvalidArgument, "unexpected connector message");
    }
  } catch (const std::exception&) {
    result = p::fail(p::ErrorCode::kCommonInternal, "connector request failed");
  }
  session->processing = false;
  if (!result) {
    session->stop(status(result.error()), "protocol_or_delivery_error");
  }
  if (session->stopped) {
    session->finish();
  } else {
    session->read();
  }
}

asio::awaitable<p::Result<void>> Gateway::quota(std::shared_ptr<SessionState> session, std::uint32_t count) {
  auto key = p::redis::Keyspace("connector-gateway", session->tenant).key("quota", session->connector);
  if (!key) {
    co_return std::unexpected(key.error());
  }
  while (!session->stopped) {
    auto lease = co_await redis_->acquire();
    if (!lease) {
      co_return std::unexpected(lease.error());
    }
    auto reply = co_await (*lease)->execute(
        {"EVAL", std::string(kQuota), "1", *key, std::to_string(count), std::to_string(session->rate)});
    if (!reply) {
      co_return std::unexpected(reply.error());
    }
    auto wait = reply->integer();
    if (!wait) {
      co_return std::unexpected(wait.error());
    }
    if (*wait == 0) {
      co_return p::Result<void>{};
    }
    if (*wait < 0 || *wait > 1000) {
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "invalid connector quota state");
    }
    asio::steady_timer delay(executor());
    delay.expires_after(std::chrono::milliseconds(std::max<std::int64_t>(*wait, 1)));
    co_await delay.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  co_return p::fail(p::ErrorCode::kCommonUnavailable, "session closed while rate limited");
}

asio::awaitable<p::Result<void>> Gateway::events(std::shared_ptr<SessionState> session) {
  const auto& batch = session->request.event_batch();
  if (batch.batch_seq() == 0 || batch.batch_seq() <= session->batch || batch.events_size() == 0
      || std::cmp_greater(batch.events_size(), session->max_batch)
      || std::cmp_greater(batch.events_size(), session->credits) || batch.ByteSizeLong() > 1048576) {
    co_return p::fail(p::ErrorCode::kIngestCreditsExceeded, "invalid connector batch or credit window");
  }
  session->batch = batch.batch_seq();
  session->credits -= static_cast<std::uint32_t>(batch.events_size());
  auto limited = co_await quota(session, static_cast<std::uint32_t>(batch.events_size()));
  if (!limited) {
    co_return limited;
  }
  Response response;
  auto* ack = response.mutable_batch_ack();
  ack->set_batch_seq(batch.batch_seq());
  std::vector<p::kafka::Record> records;
  std::set<std::string> ids;
  for (const auto& input : batch.events()) {
    if (!ids.insert(input.event_id()).second) {
      co_return p::fail(p::ErrorCode::kIngestInvalidEvent, "repeated event identity in one batch");
    }
    auto valid = validate(input);
    if (!valid) {
      auto* rejected = ack->add_rejections();
      rejected->set_event_id(input.event_id());
      rejected->set_code(std::string(valid.error().name()));
      rejected->set_message(valid.error().message());
      continue;
    }
    auto headers = p::kafka::Headers{{"traceparent", input.traceparent()}};
    auto trace = p::stream::span(context(), "accept connector event", headers);
    ingest::v1::RawEventRecord event;
    auto* metadata = event.mutable_envelope();
    envelope(metadata, session->tenant, session->connector, input.event_id());
    *metadata->mutable_occurred_at() = input.occurred_at();
    metadata->set_sequence(input.source_seq());
    metadata->set_correlation_id(input.event_id());
    auto* raw = event.mutable_raw_event();
    raw->set_connector_id(session->connector);
    raw->set_source_ref(input.source_ref());
    raw->set_source_id(
        p::stream::uuid5("728d47ed-1c69-5809-a2df-6e8b98e48ca6", {session->connector, input.source_ref()}));
    raw->set_source_epoch(input.source_epoch());
    raw->set_raw_code(input.raw_code());
    *raw->mutable_attributes() = input.attributes();
    raw->set_gateway_session_id(session->id);
    raw->set_observation_session_id(input.observation_session_id());
    if (session->state_capability && live(input, session->id, context().clock().wall())) {
      raw->set_live_observation(true);
      raw->set_observation_session_id(session->id);
    }
    p::kafka::inject(*trace, headers);
    records.push_back(record("psim.ingest.raw.v1", raw->source_id(), event, std::move(headers)));
    trace->End();
  }
  auto sent = co_await deliver(std::move(records));
  if (!sent) {
    co_return sent;
  }
  if (session->stopped || !allowed(*session)) {
    co_return p::fail(p::ErrorCode::kAuthUnauthenticated, "session closed before acknowledgement");
  }
  for (const auto& input : batch.events()) {
    if (session->state_capability && live(input, session->id, context().clock().wall()) && validate(input)) {
      if (session->sources.size() >= 10000 && !session->sources.contains(input.source_ref())) {
        co_return p::fail(p::ErrorCode::kCommonRateLimited, "state source capacity reached");
      }
      session->sources.insert(input.source_ref());
      (void)session->tracker.apply(input, {{"raw_code", input.raw_code()}});
    }
  }
  ack->set_accepted(static_cast<std::uint32_t>(batch.events_size() - ack->rejections_size()));
  accepted_->Increment(ack->accepted());
  rejected_->Increment(static_cast<double>(ack->rejections_size()));
  session->send(std::move(response));
  Response credits;
  credits.mutable_credits()->set_grant(static_cast<std::uint32_t>(batch.events_size()));
  session->send(std::move(credits));
  co_return p::Result<void>{};
}

asio::awaitable<void> Gateway::send_one(std::shared_ptr<DeliveryBatch> batch, p::kafka::Record record) {
  try {
    auto sent = co_await producer()->send(std::move(record));
    if (!sent && !batch->error) {
      batch->error = sent.error();
    }
  } catch (const std::exception&) {
    if (!batch->error) {
      batch->error = p::Error(p::ErrorCode::kCommonInternal, "Kafka delivery failed");
    }
  }
  --batch->remaining;
  batch->wake.cancel();
}

asio::awaitable<p::Result<void>> Gateway::deliver(std::vector<p::kafka::Record> records) {
  auto batch = std::make_shared<DeliveryBatch>(executor(), records.size());
  for (auto& value : records) {
    asio::co_spawn(executor(), send_one(batch, std::move(value)), asio::detached);
  }
  while (batch->remaining != 0) {
    batch->wake.expires_at(Steady::time_point::max());
    co_await batch->wake.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  if (batch->error) {
    co_return std::unexpected(*batch->error);
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Gateway::state(std::shared_ptr<SessionState> session) {
  auto report = session->request.state_report();
  auto checked = validate(report);
  if (!checked) {
    co_return checked;
  }
  if (!session->state_capability || (report.snapshot() && !session->snapshot_capability)) {
    co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "state capability was not negotiated");
  }
  auto* observations = report.mutable_observations();
  for (int i = observations->size() - 1; i >= 0; --i) {
    const auto& observation = observations->Get(i);
    const auto& source = observation.cursor().source_ref();
    if (session->sources.size() >= 10000 && !session->sources.contains(source)) {
      co_return p::fail(p::ErrorCode::kCommonRateLimited, "state source capacity reached");
    }
    session->sources.insert(source);
    if (!recent(observation.observed_at(), context().clock().wall(), std::chrono::seconds(60))
        || !session->tracker.apply(observation)) {
      observations->DeleteSubrange(i, 1);
    }
  }
  ingest::v1::GatewayStateRecord event;
  envelope(event.mutable_envelope(), session->tenant, session->connector);
  event.set_connector_id(session->connector);
  event.set_session_id(session->id);
  *event.mutable_state_report() = std::move(report);
  auto sent = co_await producer()->send(record("psim.ingest.state.v1", session->connector, event, {}));
  if (!sent) {
    co_return std::unexpected(sent.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<p::Result<void>> Gateway::result(std::shared_ptr<SessionState> session) {
  const auto& request = session->request;
  const auto id =
      request.has_command_ack() ? request.command_ack().command_id() : request.command_result().command_id();
  if (!session->command_capability || !session->commands.contains(id)) {
    co_return p::fail(p::ErrorCode::kAuthForbidden, "command does not belong to session");
  }
  response::v1::CommandResultRecord event;
  event.set_session_id(session->id);
  envelope(event.mutable_envelope(), session->tenant, session->connector);
  event.mutable_envelope()->set_correlation_id(id);
  if (request.has_command_ack()) {
    event.mutable_command_receipt()->set_command_id(id);
  } else {
    const auto& value = request.command_result();
    if (value.message().size() > 4096 || value.code().size() > 128) {
      co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "command result too large");
    }
    auto* outcome = event.mutable_command_outcome();
    outcome->set_command_id(id);
    outcome->set_success(value.success());
    outcome->set_code(value.code());
    outcome->set_message(value.message());
  }
  auto sent = co_await producer()->send(record("psim.commands.results.v1", id, event, {}));
  if (!sent) {
    co_return std::unexpected(sent.error());
  }
  co_return p::Result<void>{};
}

asio::awaitable<void> Gateway::commands() {
  while (!stopping_) {
    auto messages = co_await command_consumer_->poll(32, std::chrono::milliseconds(100));
    if (!messages) {
      healthy_.store(false);
      command_healthy_ = false;
      context().logger().warn("gateway command reader failed");
      break;
    }
    for (const auto& message : *messages) {
      auto decoded = decode<response::v1::CommandDeliveryRecord>(message);
      if (!decoded || !decoded->has_command_delivery()) {
        context().logger().warn("invalid command delivery ignored");
        continue;
      }
      const auto& command = decoded->command_delivery();
      const auto now = Util::GetCurrentTime().seconds();
      if (!p::stream::uuid(command.command_id()) || command.connector_id() != message.key()
          || !Util::IsTimestampValid(command.deadline_at()) || command.deadline_at().seconds() <= now
          || command.deadline_at().seconds() > now + 300 || command.device_external_id().empty()
          || command.device_external_id().size() > 256 || command.capability().empty()
          || command.capability().size() > 128 || command.ByteSizeLong() > 65536) {
        continue;
      }
      std::vector<std::shared_ptr<SessionState>> candidates;
      for (const auto& [id, session] : sessions_) {
        if (session->connector == command.connector_id()) {
          candidates.push_back(session);
        }
      }
      for (const auto& session : candidates) {
        if ((!command.session_id().empty() && command.session_id() != session->id) || !session->registered
            || session->stopped || !session->command_capability || session->connector != command.connector_id()
            || session->tenant != decoded->envelope().tenant_id() || !allowed(*session)) {
          continue;
        }
        auto owner = co_await registry_->owns(session->id);
        if (!owner || !*owner || session->stopped || !allowed(*session)) {
          session->stop(grpc::Status(grpc::StatusCode::UNAVAILABLE, "authoritative session lease lost"), "lease_lost");
          continue;
        }
        if (command.deadline_at().seconds() <= Util::GetCurrentTime().seconds()) {
          continue;
        }
        std::erase_if(session->commands, [now](const auto& item) { return item.second.deadline + 300 < now; });
        const auto fingerprint = p::stream::sha256(command.SerializeAsString());
        const auto found = session->commands.find(command.command_id());
        if (found != session->commands.end() && found->second.fingerprint != fingerprint) {
          context().logger().warn("conflicting command identity ignored");
          continue;
        }
        if (found == session->commands.end() && session->commands.size() >= 1024) {
          session->stop(grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "command window full"), "backpressure");
          continue;
        }
        session->commands.insert_or_assign(command.command_id(),
                                           SessionState::Command{fingerprint, command.deadline_at().seconds()});
        Response output;
        auto* target = output.mutable_command();
        target->set_command_id(command.command_id());
        target->set_device_external_id(command.device_external_id());
        target->set_capability(command.capability());
        *target->mutable_parameters() = command.parameters();
        *target->mutable_deadline_at() = command.deadline_at();
        session->send(std::move(output));
        commands_sent_->Increment();
      }
    }
  }
  --background_;
  idle_.cancel();
}

grpc::Status Gateway::Manage(grpc::ServerContext* call, const connector::v1::ManageRequest* request,
                             connector::v1::ManageResponse* response) {
  auto peer = certificate(*call->auth_context(), ca_file_);
  if (!peer
      || (peer->identity != "psim://service/gateway-admin" && peer->identity != "psim://service/resource-catalog")) {
    return {grpc::StatusCode::UNAUTHENTICATED, "management service identity required"};
  }
  const auto auth = call->client_metadata().find("authorization");
  if (auth == call->client_metadata().end()) {
    return {grpc::StatusCode::UNAUTHENTICATED, "operator token required"};
  }
  const std::string header(auth->second.data(), auth->second.size());
  if (!header.starts_with("Bearer ") || header.size() > 16384 || request->ByteSizeLong() > 65536) {
    return {grpc::StatusCode::INVALID_ARGUMENT, "invalid management request"};
  }
  try {
    auto value = asio::co_spawn(executor(), manage(*request, header.substr(7)), asio::use_future).get();
    if (!value) {
      return status(value.error());
    }
    *response = std::move(*value);
    return grpc::Status::OK;
  } catch (const std::exception&) {
    return {grpc::StatusCode::INTERNAL, "management request failed"};
  }
}

asio::awaitable<p::Result<connector::v1::ManageResponse>> Gateway::manage(connector::v1::ManageRequest request,
                                                                          std::string token) {
  if (stopping_) {
    co_return p::fail(p::ErrorCode::kCommonUnavailable, "gateway draining");
  }
  auto principal = co_await verifier_->verify(std::move(token));
  if (!principal) {
    co_return std::unexpected(principal.error());
  }
  std::optional<Certificate> cert;
  if (request.action() == connector::v1::REGISTRATION_ACTION_REGISTER
      || request.action() == connector::v1::REGISTRATION_ACTION_ROTATE
      || request.action() == connector::v1::REGISTRATION_ACTION_RECOVER) {
    auto parsed = certificate(request.certificate_pem(), ca_file_);
    if (parsed) {
      cert = std::move(*parsed);
    }
    // The registry resolves an exact persisted retry before validating a new certificate change.
  }
  auto saved = co_await registry_->manage(std::move(request), std::move(*principal), std::move(cert));
  if (!saved) {
    co_return std::unexpected(saved.error());
  }
  const auto deadline = Steady::now() + std::chrono::seconds(15);
  while (!stopping_ && Steady::now() < deadline) {
    auto enforced = co_await registry_->enforced(saved->revision());
    if (!enforced) {
      co_return std::unexpected(enforced.error());
    }
    if (*enforced) {
      co_return saved;
    }
    asio::steady_timer wait(executor());
    wait.expires_after(std::chrono::milliseconds(50));
    co_await wait.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  co_return p::fail(p::ErrorCode::kCommonDeadlineExceeded,
                    "registration committed; enforcement still pending, retry the same request identity");
}
}  // namespace psim::connector_gateway
