#include <boost/asio/as_tuple.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <psim/api/v1/feed.pb.h>
#include <psim/api/v1/me.pb.h>
#include <psim/audit/v1/audit.pb.h>
#include <psim/incident/v1/events.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "metrics.hpp"
#include "proxy.hpp"
#include "psim/common/v1/envelope.pb.h"
#include "psim/incident/v1/incident.pb.h"
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/realtime/v1/realtime.pb.h"
#include "routes.hpp"

namespace psim::api_gateway {
namespace p = platform;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ws = beast::websocket;
using Tcp = asio::ip::tcp;
using Json = nlohmann::json;
class Gateway;

class SessionBase {
 public:
  virtual ~SessionBase() = default;
  virtual void stop() = 0;
  virtual void send(const realtime::v1::ServerFrame&) = 0;
  virtual void delta(const incident::v1::IncidentEventRecord&, const std::string&) = 0;
  asio::cancellation_signal cancel;
  std::optional<p::security::Principal> principal;
  std::string subscription;
};

template <typename Stream>
class Session : public SessionBase, public std::enable_shared_from_this<Session<Stream>> {
 public:
  Session(Gateway* owner, Stream connection);
  asio::awaitable<void> run();
  asio::awaitable<void> write();
  void send(const realtime::v1::ServerFrame& frame) override;
  void stop() override;
  void delta(const incident::v1::IncidentEventRecord& event, const std::string& token) override;
  Gateway* gateway;
  ws::stream<Stream> socket;
  asio::steady_timer wake;
  std::deque<std::string> queue;
  std::size_t queued_bytes = 0;
  bool closed = false, upgraded = false;
};

struct JournalEntry {
  incident::v1::IncidentEventRecord event;
  std::string partition;
  std::int64_t offset;
  std::string token;
};

class Gateway final : public p::stream::Worker {
 public:
  explicit Gateway(const p::runtime::Context& context)
      : p::stream::Worker(context, "projection", "psim.incidents.events.v1", p::kafka::Semantics::kAtLeastOnce, true),
        acceptor_(executor()),
        idle_(executor()),
        relay_timer_(executor()),
        epoch_(p::stream::uuid7()) {}

  asio::awaitable<p::Result<void>> initialize() override {
    auto registered = co_await schema("psim.audit.v1");
    if (!registered) {
      co_return registered;
    }
    http_metrics_ = std::make_unique<HttpMetrics>(context());
    const auto& config = context().config();
    origins_ = config.get<std::vector<std::string>>("settings.allowed_origins");
    limiter_ = std::make_unique<RateLimiter>(config.get<std::size_t>("settings.rate_per_second"),
                                             config.get<std::size_t>("settings.rate_burst"));
    ip_limiter_ = std::make_unique<RateLimiter>(config.get<std::size_t>("settings.ip_rate_per_second"),
                                                config.get<std::size_t>("settings.ip_rate_burst"));
    for (const auto* name : {"catalog", "rules", "incidents", "commands", "response", "history", "reports",
                             "projection", "audit", "notifications"}) {
      endpoints_.emplace(name, Endpoint{config.get<std::string>(std::string("settings.") + name + "_host"),
                                        config.get<std::string>(std::string("settings.") + name + "_port")});
    }
    auto upstream = co_await context().blocking().run([&config] {
      return p::security::tls_context(
          {.ca_file = config.get<std::string>("settings.upstream_ca_file"),
           .certificate_file = config.get<std::string>("settings.upstream_certificate_file"),
           .private_key_file = config.get<std::string>("settings.upstream_key_file"),
           .require_client_certificate = false});
    });
    if (!upstream) {
      co_return std::unexpected(upstream.error());
    }
    upstream_tls_ = std::move(*upstream);
    plaintext_ = config.get<bool>("settings.listen_plaintext");
    if (plaintext_ && !config.get<bool>("settings.allow_plaintext")) {
      co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "plaintext listener requires development opt-in");
    }
    if (!plaintext_) {
      auto tls = co_await context().blocking().run([&config] {
        return p::security::tls_context({.boundary = p::security::Boundary::kExternal,
                                         .ca_file = config.get<std::string>("settings.tls_ca_file"),
                                         .certificate_file = config.get<std::string>("settings.tls_certificate_file"),
                                         .private_key_file = config.get<std::string>("settings.tls_private_key_file"),
                                         .require_client_certificate = false},
                                        true);
      });
      if (!tls) {
        co_return std::unexpected(tls.error());
      }
      tls_ = std::move(*tls);
    }
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    auto lease = co_await pool_->acquire();
    if (!lease) {
      co_return std::unexpected(lease.error());
    }
    auto initialized = co_await p::pg::MessageStore("access").initialize(&**lease);
    if (!initialized) {
      co_return initialized;
    }
    auto verifier = p::security::JwksCache::create(
        {.jwt = {.issuer = context().config().get<std::string>("settings.issuer"), .audience = "psim"},
         .url = context().config().get<std::string>("settings.jwks_url"),
         .http = {
             .tls = {.ca_file = config.get<std::string>("settings.jwks_ca_file"), .require_client_certificate = false},
             .allow_plaintext = config.get<bool>("settings.allow_plaintext")}});
    if (!verifier) {
      co_return std::unexpected(verifier.error());
    }
    verifier_ = std::move(*verifier);
    auto keys = co_await verifier_->refresh();
    if (!keys) {
      co_return keys;
    }
    relay_ = std::make_unique<p::pg::OutboxRelay>(pool_.get(), p::pg::MessageStore("access"),
                                                  p::pg::kafka_publisher(producer()));
    relaying_ = true;
    asio::co_spawn(executor(), relay(), [this](const std::exception_ptr& error) {
      (void)error;
      relaying_ = false;
      idle_.cancel();
    });
    acceptor_.open(Tcp::v4());
    acceptor_.set_option(Tcp::acceptor::reuse_address(true));
    acceptor_.bind({Tcp::v4(), static_cast<unsigned short>(context().config().get<int>("settings.listen_port"))});
    acceptor_.listen(64);
    listening_ = true;
    accepting_ = true;
    asio::co_spawn(executor(), accept(), asio::detached);
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<p::security::Principal>> authenticate(std::string token) {
    auto principal = co_await verifier_->verify(std::move(token));
    if (!principal) {
      co_return std::unexpected(principal.error());
    }
    if (!p::stream::uuid(principal->tenant)) {
      co_return p::fail(p::ErrorCode::kAuthForbidden, "tenant identity required");
    }
    co_return principal;
  }

  std::string token(const std::map<std::string, std::int64_t>& positions, std::string tenant) const {
    return Json{{"epoch", epoch_}, {"tenant", std::move(tenant)}, {"offsets", positions}}.dump();
  }

  void subscribe(SessionBase* session, const realtime::v1::Subscribe& request, const std::string& request_id) {
    if (!session->principal) {
      return;
    }
    realtime::v1::ServerFrame frame;
    frame.set_request_id(request_id);
    bool valid = false;
    std::map<std::string, std::int64_t> offsets;
    try {
      const auto parsed = Json::parse(request.resume_token());
      offsets = parsed.at("offsets").get<std::map<std::string, std::int64_t>>();
      valid = parsed.at("epoch") == epoch_ && parsed.at("tenant") == session->principal->tenant && offsets.size() <= 64;
      for (const auto& [partition, offset] : floor_) {
        if (offsets[partition] < offset) {
          valid = false;
        }
      }
      for (const auto& [partition, offset] : offsets) {
        const auto known = offsets_.find(partition);
        const bool empty_partition =
            consumer() != nullptr && std::ranges::any_of(consumer()->positions(), [&](const auto& entry) {
              return entry.first.topic == "psim.incidents.events.v1"
                     && std::to_string(entry.first.partition) == partition && entry.second == offset;
            });
        if (offset < 0 || (known == offsets_.end() ? !empty_partition : offset > known->second)) {
          valid = false;
        }
      }
    } catch (const std::exception&) {
      valid = false;
    }
    if (request.stream() != realtime::v1::STREAM_INCIDENTS || request.subscription_id().empty()
        || request.subscription_id().size() > 128 || request.filter().site_ids_size() != 0
        || request.filter().zone_ids_size() != 0 || !request.filter().incident_id().empty()
        || request.filter().include_closed()) {
      valid = false;
    }
    if (!valid) {
      frame.mutable_resync_required()->set_subscription_id(request.subscription_id());
      frame.mutable_resync_required()->set_reason("snapshot expired or unsupported fixture scope");
      session->send(frame);
      return;
    }
    session->subscription = request.subscription_id();
    frame.mutable_subscribed()->set_subscription_id(request.subscription_id());
    frame.mutable_subscribed()->set_resume_token(request.resume_token());
    session->send(frame);
    for (const auto& entry : journal_) {
      if (entry.offset >= offsets[entry.partition]) {
        session->delta(entry.event, entry.token);
      }
    }
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* /*output*/) override {
    auto trace = p::stream::span(context(), "deliver incident", message->headers());
    auto event = decode<incident::v1::IncidentEventRecord>(*message);
    if (!event) {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "unsupported incident event");
    }
    const auto partition = std::to_string(message->partition());
    if (message->offset() < offsets_[partition]) {
      trace->End();
      co_return p::Result<void>{};
    }
    floor_.try_emplace(partition, message->offset());
    offsets_[partition] = std::max(offsets_[partition], message->offset() + 1);
    const auto resume = token(offsets_, event->envelope().tenant_id());
    journal_.push_back({*event, partition, message->offset(), resume});
    if (journal_.size() > 256) {
      const auto& old = journal_.front();
      floor_[old.partition] = old.offset + 1;
      journal_.pop_front();
    }
    for (const auto& session : sessions_) {
      session->delta(*event, resume);
    }
    trace->End();
    co_return p::Result<void>{};
  }

  asio::awaitable<void> accept() {
    while (accepting_) {
      auto [error, socket] = co_await acceptor_.async_accept(asio::as_tuple(asio::use_awaitable));
      if (error) {
        break;
      }
      if (sessions_.size() >= 32) {
        socket.close();
        continue;
      }
      if (!ip_limiter_->allow(socket.remote_endpoint().address().to_string(), std::chrono::steady_clock::now())) {
        http_metrics_->limited();
        socket.close();
        continue;
      }
      if (plaintext_) {
        launch(std::make_shared<Session<beast::tcp_stream>>(this, beast::tcp_stream(std::move(socket))));
      } else {
        launch(std::make_shared<Session<beast::ssl_stream<beast::tcp_stream>>>(
            this, beast::ssl_stream<beast::tcp_stream>(std::move(socket), *tls_)));
      }
    }
    accepting_ = false;
    listening_ = false;
    idle_.cancel();
  }

  asio::awaitable<void> shutdown() override {
    accepting_ = false;
    boost::system::error_code ignored;
    (void)acceptor_.close(ignored);
    for (const auto& session : sessions_) {
      session->stop();
    }
    relay_timer_.cancel();
    while (!sessions_.empty() || listening_ || relaying_) {
      idle_.expires_after(std::chrono::milliseconds(10));
      co_await idle_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
    verifier_.reset();
  }

  template <typename Stream>
  void launch(std::shared_ptr<Session<Stream>> session) {
    sessions_.insert(session);
    asio::co_spawn(
        executor(), session->run(),
        asio::bind_cancellation_slot(session->cancel.slot(), [this, session](const std::exception_ptr& failure) {
          if (failure) {
            try {
              std::rethrow_exception(failure);
            } catch (const std::exception& error) {
              if (!session->closed) {
                context().logger().warn("API session failed", {{"error", error.what()}});
              }
            }
            session->stop();
          }
          sessions_.erase(session);
          idle_.cancel();
        }));
  }

  asio::awaitable<void> relay() {
    while (accepting_ || !listening_) {
      if (!relaying_) {
        co_return;
      }
      auto result = co_await relay_->drain_once();
      (void)result;
      relay_timer_.expires_after(std::chrono::milliseconds(100));
      co_await relay_timer_.async_wait(asio::as_tuple(asio::use_awaitable));
      if (!accepting_) {
        break;
      }
    }
  }

  asio::awaitable<p::Result<void>> denied(std::optional<p::security::Principal> principal, std::string operation) {
    http_metrics_->denied();
    audit::v1::AuditEventRecord event;
    auto* env = event.mutable_envelope();
    env->set_message_id(p::stream::uuid7());
    env->set_correlation_id(env->message_id());
    env->set_schema_version("1");
    env->set_tenant_id(principal ? principal->tenant : "00000000-0000-0000-0000-000000000000");
    *env->mutable_occurred_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
    *env->mutable_produced_at() = env->occurred_at();
    auto* entry = event.mutable_audit_entry();
    entry->mutable_actor()->set_kind(principal ? common::v1::ACTOR_KIND_USER : common::v1::ACTOR_KIND_SERVICE);
    entry->mutable_actor()->set_id(principal ? principal->subject : "api-gateway");
    *env->mutable_actor() = entry->actor();
    entry->set_action("access.denied");
    entry->set_object_type("rest-operation");
    entry->set_object_id(std::move(operation));
    entry->set_outcome(audit::v1::AUDIT_OUTCOME_DENIED);
    auto message = record("psim.audit.v1", env->tenant_id(), event, {});
    co_return co_await p::pg::in_transaction<void>(
        pool_.get(),
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited closure.
        [&event, &message](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          co_return co_await p::pg::MessageStore("access").append(
              tx, {.id = event.envelope().message_id(), .record = std::move(message)});
        });
  }

  bool origin(std::string_view value) const { return allowed_origin(value, origins_); }

  bool limit(const p::security::Principal& principal) {
    const bool allowed = limiter_->allow(principal.tenant + ":" + principal.subject, std::chrono::steady_clock::now());
    if (!allowed) {
      http_metrics_->limited();
    }
    return allowed;
  }

  const Endpoint& endpoint(const std::string& service) const { return endpoints_.at(service); }

  asio::ssl::context& upstream_tls() { return *upstream_tls_; }

  HttpMetrics& metrics() { return *http_metrics_; }

  const std::string& epoch() const { return epoch_; }

 private:
  Tcp::acceptor acceptor_;
  asio::steady_timer idle_, relay_timer_;
  bool plaintext_ = false, relaying_ = false;
  std::shared_ptr<asio::ssl::context> tls_, upstream_tls_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  std::unique_ptr<RateLimiter> limiter_, ip_limiter_;
  std::unique_ptr<HttpMetrics> http_metrics_;
  std::vector<std::string> origins_;
  std::map<std::string, Endpoint> endpoints_;
  bool accepting_ = false;
  bool listening_ = false;
  std::string epoch_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::map<std::string, std::int64_t> offsets_, floor_;
  std::deque<JournalEntry> journal_;
  std::set<std::shared_ptr<SessionBase>> sessions_;
};

template <typename Stream>
Session<Stream>::Session(Gateway* owner, Stream connection)
    : gateway(owner), socket(std::move(connection)), wake(socket.get_executor()) {}

template <typename Stream>
void Session<Stream>::stop() {
  if (closed) {
    return;
  }
  closed = true;
  cancel.emit(asio::cancellation_type::terminal);
  wake.cancel();
  boost::system::error_code ignored;
  (void)beast::get_lowest_layer(socket).socket().cancel(ignored);
  (void)beast::get_lowest_layer(socket).socket().close(ignored);
}

template <typename Stream>
void Session<Stream>::send(const realtime::v1::ServerFrame& frame) {
  if (closed) {
    return;
  }
  auto text = p::stream::json(frame);
  if (queue.size() >= 256 || queued_bytes + text.size() > 1048576) {
    stop();
    return;
  }
  queued_bytes += text.size();
  queue.push_back(std::move(text));
  wake.cancel();
}

template <typename Stream>
void Session<Stream>::delta(const incident::v1::IncidentEventRecord& event, const std::string& token) {
  if (!principal || subscription.empty() || principal->tenant != event.envelope().tenant_id()) {
    return;
  }
  const auto* payload = incident::v1::IncidentEventRecord::GetReflection()->GetOneofFieldDescriptor(
      event, incident::v1::IncidentEventRecord::GetDescriptor()->FindOneofByName("payload"));
  if (!payload) {
    return;
  }
  const auto& change = incident::v1::IncidentEventRecord::GetReflection()->GetMessage(event, payload);
  const auto* field = change.GetDescriptor()->FindFieldByName("incident");
  if (!field) {
    return;
  }
  incident::v1::Incident value;
  value.CopyFrom(change.GetReflection()->GetMessage(change, field));
  if (!p::authz::scoped(*principal, {principal->tenant, value.site_id(), value.zone_id()})) {
    return;
  }
  realtime::v1::ServerFrame frame;
  auto* delta = frame.mutable_delta();
  delta->set_subscription_id(subscription);
  delta->set_resume_token(token);
  *delta->mutable_incident_event() = event;
  send(frame);
}

template <typename Stream>
asio::awaitable<void> Session<Stream>::write() {
  while (!closed) {
    if (principal && principal->expires_at <= std::chrono::system_clock::now()) {
      stop();
      break;
    }
    if (queue.empty()) {
      wake.expires_after(std::chrono::seconds(1));
      co_await wake.async_wait(asio::as_tuple(asio::use_awaitable));
      continue;
    }
    auto text = std::move(queue.front());
    queue.pop_front();
    queued_bytes -= text.size();
    const auto [error, size] = co_await socket.async_write(asio::buffer(text), asio::as_tuple(asio::use_awaitable));
    (void)size;
    if (error) {
      stop();
      break;
    }
  }
}

template <typename Stream>
asio::awaitable<void> Session<Stream>::run() {
  beast::get_lowest_layer(socket).expires_after(std::chrono::seconds(10));
  beast::flat_buffer buffer;
  if constexpr (!std::is_same_v<Stream, beast::tcp_stream>) {
    auto [handshake] = co_await socket.next_layer().async_handshake(asio::ssl::stream_base::server,
                                                                    asio::as_tuple(asio::use_awaitable));
    if (handshake) {
      stop();
      co_return;
    }
  }
  http::request_parser<http::buffer_body> parser;
  parser.body_limit(64ULL * 1024 * 1024);
  parser.header_limit(16384);
  auto [error, bytes] =
      co_await http::async_read_header(socket.next_layer(), buffer, parser, asio::as_tuple(asio::use_awaitable));
  (void)bytes;
  if (error) {
    const auto status = error == http::error::body_limit ? http::status::payload_too_large : http::status::bad_request;
    http::response<http::string_body> response(status, 11);
    response.set(http::field::content_type, "application/problem+json");
    security_headers(response.base());
    response.body() =
        Json{{"type", "about:blank"},
             {"title", "Invalid request"},
             {"status", response.result_int()},
             {"code", error == http::error::body_limit ? "COMMON_PAYLOAD_TOO_LARGE" : "COMMON_INVALID_ARGUMENT"}}
            .dump();
    response.prepare_payload();
    co_await http::async_write(socket.next_layer(), response, asio::as_tuple(asio::use_awaitable));
    stop();
    co_return;
  }
  const auto& request = parser.get();
  if (ws::is_upgrade(request) && request.target() == "/v1/realtime") {
    const std::string origin(request[http::field::origin]);
    if (origin.empty() || !gateway->origin(origin)) {
      stop();
      co_return;
    }
    if (!parser.is_done()) {
      stop();
      co_return;
    }
    http::request<http::empty_body> upgrade(request.method(), request.target(), request.version());
    upgrade.base() = request.base();
    socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::server));
    socket.read_message_max(16384);
    beast::get_lowest_layer(socket).expires_never();
    const auto [accepted] = co_await socket.async_accept(upgrade, asio::as_tuple(asio::use_awaitable));
    if (accepted) {
      stop();
      co_return;
    }
    upgraded = true;
    asio::co_spawn(socket.get_executor(), write(),
                   [self = this->shared_from_this()](const std::exception_ptr&) { self->stop(); });
    while (!closed) {
      buffer.consume(buffer.size());
      const auto [read_error, read_bytes] = co_await socket.async_read(buffer, asio::as_tuple(asio::use_awaitable));
      (void)read_bytes;
      if (read_error) {
        break;
      }
      realtime::v1::ClientFrame frame;
      if (!google::protobuf::util::JsonStringToMessage(beast::buffers_to_string(buffer.data()), &frame).ok()) {
        break;
      }
      realtime::v1::ServerFrame response;
      response.set_request_id(frame.request_id());
      if (!frame.has_authenticate() && (!principal || principal->expires_at <= std::chrono::system_clock::now())) {
        break;
      }
      if (frame.has_authenticate()) {
        auto authenticated = co_await gateway->authenticate(frame.authenticate().access_token());
        if (!authenticated || !p::authz::decide_role(*authenticated, "feed.read", std::chrono::system_clock::now())) {
          auto logged = co_await gateway->denied(authenticated ? std::optional(*authenticated) : std::nullopt,
                                                 "realtime.authenticate");
          (void)logged;
          break;
        }
        principal = std::move(*authenticated);
        response.mutable_authenticated()->set_user_id(principal->subject);
        response.mutable_authenticated()->set_expires_in(static_cast<std::uint32_t>(std::max<std::int64_t>(
            0,
            std::chrono::duration_cast<std::chrono::seconds>(principal->expires_at - std::chrono::system_clock::now())
                .count())));
        send(response);
      } else if (frame.has_subscribe()) {
        gateway->subscribe(this, frame.subscribe(), frame.request_id());
      } else if (frame.has_unsubscribe()) {
        subscription.clear();
      } else if (frame.has_ping()) {
        response.mutable_pong();
        send(response);
      } else {
        break;
      }
    }
    stop();
    co_return;
  }
  const std::string origin(request[http::field::origin]);
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure lives in the awaiting session.
  auto send = [&](http::status status, std::string body,
                  std::string type = "application/json") -> asio::awaitable<void> {
    http::response<http::string_body> response(status, request.version());
    response.keep_alive(false);
    response.set(http::field::content_type, type);
    security_headers(response.base(), gateway->origin(origin) ? origin : "");
    if (status == http::status::unauthorized) {
      response.set(http::field::www_authenticate, "Bearer");
    }
    if (status == http::status::too_many_requests) {
      response.set(http::field::retry_after, "1");
    }
    if (request.method() == http::verb::options && status == http::status::no_content) {
      response.set(http::field::access_control_allow_methods, "GET, POST, PUT, OPTIONS");
      response.set(http::field::access_control_allow_headers,
                   "Authorization, Content-Type, Idempotency-Key, If-Match, If-None-Match, traceparent");
    }
    response.body() = std::move(body);
    response.prepare_payload();
    co_await http::async_write(socket.next_layer(), response, asio::as_tuple(asio::use_awaitable));
  };
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure lives in the awaiting session.
  auto problem = [&](p::Error failure) -> asio::awaitable<void> {
    const auto& info = p::info(failure.code());
    co_await send(
        static_cast<http::status>(info.http_status),
        Json{{"type", "about:blank"}, {"title", info.title}, {"status", info.http_status}, {"code", failure.name()}}
            .dump(),
        "application/problem+json");
  };
  auto selected = route(request.method() == http::verb::options
                            ? http::string_to_verb(request[http::field::access_control_request_method])
                            : request.method(),
                        request.target());
  const auto measurement =
      gateway->metrics().start(selected ? (*selected)->operation : "unmatched", &parser.get().base());
  if (!gateway->origin(origin)) {
    auto logged = co_await gateway->denied(std::nullopt, "cors");
    co_await problem(logged ? p::Error(p::ErrorCode::kAuthForbidden, "origin denied") : logged.error());
  } else if (!selected) {
    co_await problem(selected.error());
  } else if (request.method() == http::verb::options) {
    co_await send(http::status::no_content, "");
  } else {
    p::Result<p::security::Principal> authenticated =
        p::fail(p::ErrorCode::kAuthUnauthenticated, "Bearer token required");
    const std::string authorization(request[http::field::authorization]);
    if (authorization.starts_with("Bearer ") && authorization.size() <= 8192
        && request.count(http::field::authorization) == 1) {
      authenticated = co_await gateway->authenticate(authorization.substr(7));
    }
    p::Result<void> access;
    if (!authenticated) {
      access = std::unexpected(authenticated.error());
    } else if (!(*selected)->action.empty()) {
      access = p::authz::decide_role(*authenticated, (*selected)->action, std::chrono::system_clock::now());
    }
    if (access) {
      beast::get_lowest_layer(socket).expires_after(std::chrono::seconds(60));
    }
    if (!access) {
      auto logged = co_await gateway->denied(authenticated ? std::optional(*authenticated) : std::nullopt,
                                             (*selected)->operation);
      co_await problem(logged ? access.error() : logged.error());
    } else if (!gateway->limit(*authenticated)) {
      co_await problem(p::Error(p::ErrorCode::kCommonRateLimited, "request rate exceeded"));
    } else if (auto headers = request_headers(**selected, request.base()); !headers) {
      co_await problem(headers.error());
    } else if ((*selected)->service == "local") {
      if (!parser.is_done()) {
        co_await problem(p::Error(p::ErrorCode::kCommonInvalidArgument, "body not supported"));
      } else if ((*selected)->operation == "MeService_GetMe") {
        if (request.target().find('?') != std::string_view::npos) {
          co_await problem(p::Error(p::ErrorCode::kCommonInvalidArgument, "query not supported"));
          stop();
          co_return;
        }
        api::v1::GetMeResponse me;
        me.set_user_id(authenticated->subject);
        me.set_tenant_id(authenticated->tenant);
        for (const auto& role : authenticated->roles) {
          me.add_roles(role);
        }
        for (const auto& scope : authenticated->scopes) {
          if (scope.starts_with("site:")) {
            me.add_site_ids(scope.substr(5));
          } else if (scope.starts_with("zone:")) {
            me.add_zone_ids(scope.substr(5));
          } else {
            me.add_site_ids(scope);
          }
        }
        for (const auto& action : p::authz::actions(*authenticated, std::chrono::system_clock::now())) {
          me.add_actions(action);
        }
        co_await send(http::status::ok, p::stream::json(me));
      }
    } else if (parser.content_length().value_or(0) > request_limit(**selected)) {
      co_await problem(p::Error(p::ErrorCode::kCommonPayloadTooLarge, "request body exceeds limit"));
    } else {
      parser.body_limit(request_limit(**selected));
      bool started = false;
      bool forwarded = false;
      bool invalid_body = false;
      bool oversized = false;
      if (request[http::field::expect] == "100-continue") {
        const http::response<http::empty_body> interim(http::status::continue_, 11);
        co_await http::async_write(socket.next_layer(), interim, asio::as_tuple(asio::use_awaitable));
      }
      try {
        forwarded = co_await proxy(&socket.next_layer(), &buffer, &parser, gateway->endpoint((*selected)->service),
                                   &gateway->upstream_tls(), origin, gateway->epoch(), (*selected)->floor_plan,
                                   &started, &invalid_body, &oversized);
      } catch (const std::exception&) {
        forwarded = false;
      }
      if (!forwarded && !invalid_body && !oversized && !closed) {
        gateway->metrics().proxy_failed();
      }
      if (!forwarded && !started) {
        auto code = p::ErrorCode::kCommonUnavailable;
        if (invalid_body) {
          code = p::ErrorCode::kCommonInvalidArgument;
        }
        if (oversized) {
          code = p::ErrorCode::kCommonPayloadTooLarge;
        }
        co_await problem(p::Error(code, "upstream request failed"));
      }
    }
  }
  if constexpr (!std::is_same_v<Stream, beast::tcp_stream>) {
    beast::get_lowest_layer(socket).expires_after(std::chrono::seconds(2));
    (void)co_await socket.next_layer().async_shutdown(asio::as_tuple(asio::use_awaitable));
  }
  stop();
}

}  // namespace psim::api_gateway

namespace psim::api_gateway {
namespace {
std::string settings() {
  auto schema = Json::parse(p::stream::kSettings);
  auto& fields = schema["properties"];
  fields["listen_plaintext"] = {{"type", "boolean"}, {"default", false}};
  fields["allowed_origins"] = {{"type", "array"}, {"items", {{"type", "string"}}}, {"default", Json::array()}};
  for (const auto* name : {"tls_ca_file", "tls_certificate_file", "tls_private_key_file", "upstream_ca_file",
                           "upstream_certificate_file", "upstream_key_file", "jwks_ca_file", "redis_host",
                           "redis_ca_file", "redis_certificate_file", "redis_key_file"}) {
    fields[name] = {{"type", "string"}, {"default", ""}};
  }
  fields["redis_password_file"] = {{"type", "string"}, {"x-psim-secret", true}};
  for (const auto* name : {"rate_per_second", "rate_burst", "ip_rate_per_second", "ip_rate_burst"}) {
    fields[name] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 100000}, {"default", 100}};
  }
  for (const auto& [name, host] : std::map<std::string, std::string>{{"catalog", "resource-catalog"},
                                                                     {"rules", "rule-management"},
                                                                     {"incidents", "incident-service"},
                                                                     {"commands", "command-service"},
                                                                     {"response", "response-engine"},
                                                                     {"history", "event-history"},
                                                                     {"reports", "projection-service"},
                                                                     {"projection", "projection-service"},
                                                                     {"audit", "audit-service"},
                                                                     {"notifications", "notification-service"}}) {
    fields[name + "_host"] = {{"type", "string"}, {"minLength", 1}, {"default", host}};
    fields[name + "_port"] = {{"type", "string"}, {"pattern", "^[0-9]{1,5}$"}, {"default", "8443"}};
  }
  return schema.dump();
}
}  // namespace
}  // namespace psim::api_gateway

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "api-gateway",
                                       .settings_schema = psim::api_gateway::settings(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::api_gateway::Gateway>(context));
                                         return result;
                                       }});
}
