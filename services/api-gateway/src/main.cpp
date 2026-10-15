#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <google/protobuf/util/json_util.h>
#include <nlohmann/json.hpp>
#include <psim/api/v1/feed.pb.h>
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
#include <utility>

#include "projection.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/realtime/v1/realtime.pb.h"

namespace psim::api_gateway {
namespace p = platform;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ws = beast::websocket;
using Tcp = asio::ip::tcp;
using Json = nlohmann::json;
class Gateway;

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(Gateway* owner, Tcp::socket connection);
  asio::awaitable<void> run();
  asio::awaitable<void> write();
  void send(const realtime::v1::ServerFrame& frame);
  void stop();
  void delta(const incident::v1::IncidentEventRecord& event, const std::string& token);
  Gateway* gateway;
  ws::stream<beast::tcp_stream> socket;
  asio::steady_timer wake;
  std::deque<std::string> queue;
  std::size_t queued_bytes = 0;
  std::optional<p::security::Principal> principal;
  std::string subscription;
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
        epoch_(p::stream::uuid7()) {}

  asio::awaitable<p::Result<void>> initialize() override {
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    projection_ = std::make_unique<Projection>(pool_.get(), context().config().get<std::string>("service.instance"));
    auto initialized = co_await projection_->initialize();
    if (!initialized) {
      co_return initialized;
    }
    auto snapshot = co_await projection_->snapshot("11111111-1111-4111-8111-111111111111");
    if (!snapshot) {
      co_return std::unexpected(snapshot.error());
    }
    offsets_ = std::move(snapshot->offsets);
    floor_ = offsets_;
    auto verifier = p::security::JwksCache::create(
        {.jwt = {.issuer = context().config().get<std::string>("settings.issuer"), .audience = "psim"},
         .url = context().config().get<std::string>("settings.jwks_url"),
         .http = {.allow_plaintext = context().config().get<bool>("settings.allow_plaintext")}});
    if (!verifier) {
      co_return std::unexpected(verifier.error());
    }
    verifier_ = std::move(*verifier);
    auto keys = co_await verifier_->refresh();
    if (!keys) {
      co_return keys;
    }
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
    if (principal->tenant != "11111111-1111-4111-8111-111111111111"
        || (!principal->roles.contains("operator") && !principal->roles.contains("supervisor"))) {
      co_return p::fail(p::ErrorCode::kAuthForbidden, "incident feed access denied");
    }
    co_return principal;
  }

  std::string token(const std::map<std::string, std::int64_t>& positions) const {
    return Json{{"epoch", epoch_}, {"tenant", "11111111-1111-4111-8111-111111111111"}, {"offsets", positions}}.dump();
  }

  asio::awaitable<p::Result<api::v1::GetIncidentFeedResponse>> snapshot(std::string tenant) {
    auto result = co_await projection_->snapshot(std::move(tenant));
    if (!result) {
      co_return std::unexpected(result.error());
    }
    result->feed.set_resume_token(token(result->offsets));
    co_return std::move(result->feed);
  }

  void subscribe(Session* session, const realtime::v1::Subscribe& request, const std::string& request_id) {
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
        if (known == offsets_.end() || offset < 0 || offset > known->second) {
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
    auto trace = p::stream::span(context(), "project and deliver incident", message->headers());
    auto event = decode<incident::v1::IncidentEventRecord>(*message);
    if (!event || (!event->has_incident_created() && !event->has_signal_attached())) {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "unsupported incident event");
    }
    auto applied = co_await projection_->apply(*event, message);
    if (!applied) {
      trace->End();
      co_return applied;
    }
    const auto partition = std::to_string(message->partition());
    if (message->offset() < offsets_[partition]) {
      trace->End();
      co_return p::Result<void>{};
    }
    offsets_[partition] = std::max(offsets_[partition], message->offset() + 1);
    const auto resume = token(offsets_);
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
      auto session = std::make_shared<Session>(this, std::move(socket));
      sessions_.insert(session);
      asio::co_spawn(executor(), session->run(), [this, session](const std::exception_ptr& failure) {
        if (failure) {
          session->stop();
        }
        sessions_.erase(session);
        idle_.cancel();
      });
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
    while (!sessions_.empty() || listening_) {
      idle_.expires_after(std::chrono::milliseconds(10));
      co_await idle_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    projection_.reset();
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
    verifier_.reset();
  }

 private:
  Tcp::acceptor acceptor_;
  asio::steady_timer idle_;
  bool accepting_ = false;
  bool listening_ = false;
  std::string epoch_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<Projection> projection_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::map<std::string, std::int64_t> offsets_, floor_;
  std::deque<JournalEntry> journal_;
  std::set<std::shared_ptr<Session>> sessions_;
};

Session::Session(Gateway* owner, Tcp::socket connection)
    : gateway(owner), socket(std::move(connection)), wake(socket.get_executor()) {}

void Session::stop() {
  if (closed) {
    return;
  }
  closed = true;
  wake.cancel();
  boost::system::error_code ignored;
  (void)beast::get_lowest_layer(socket).socket().cancel(ignored);
  (void)beast::get_lowest_layer(socket).socket().close(ignored);
}

void Session::send(const realtime::v1::ServerFrame& frame) {
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

void Session::delta(const incident::v1::IncidentEventRecord& event, const std::string& token) {
  if (!principal || subscription.empty() || principal->tenant != event.envelope().tenant_id()) {
    return;
  }
  realtime::v1::ServerFrame frame;
  auto* delta = frame.mutable_delta();
  delta->set_subscription_id(subscription);
  delta->set_resume_token(token);
  *delta->mutable_incident_event() = event;
  send(frame);
}

asio::awaitable<void> Session::write() {
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

asio::awaitable<void> Session::run() {
  beast::get_lowest_layer(socket).expires_after(std::chrono::seconds(10));
  beast::flat_buffer buffer;
  http::request_parser<http::string_body> parser;
  parser.body_limit(16384);
  parser.header_limit(16384);
  auto [error, bytes] =
      co_await http::async_read(socket.next_layer(), buffer, parser, asio::as_tuple(asio::use_awaitable));
  (void)bytes;
  if (error) {
    stop();
    co_return;
  }
  const auto request = parser.release();
  if (ws::is_upgrade(request) && request.target() == "/v1/realtime") {
    const std::string origin(request[http::field::origin]);
    if (origin != "http://localhost:5173" && origin != "http://localhost:4173" && origin != "http://console:4173") {
      stop();
      co_return;
    }
    socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::server));
    socket.read_message_max(16384);
    beast::get_lowest_layer(socket).expires_never();
    const auto [accepted] = co_await socket.async_accept(request, asio::as_tuple(asio::use_awaitable));
    if (accepted) {
      stop();
      co_return;
    }
    upgraded = true;
    asio::co_spawn(socket.get_executor(), write(),
                   [self = shared_from_this()](const std::exception_ptr&) { self->stop(); });
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
        if (!authenticated) {
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
  http::response<http::string_body> response(http::status::ok, request.version());
  response.keep_alive(false);
  response.set(http::field::content_type, "application/json");
  response.set(http::field::cache_control, "no-store");
  p::Result<p::security::Principal> authenticated =
      p::fail(p::ErrorCode::kAuthUnauthenticated, "Bearer token required");
  const std::string authorization(request[http::field::authorization]);
  if (authorization.starts_with("Bearer ") && authorization.size() <= 8192) {
    authenticated = co_await gateway->authenticate(authorization.substr(7));
  }
  if (!authenticated) {
    response.result(static_cast<http::status>(p::info(authenticated.error().code()).http_status));
    response.set(http::field::content_type, "application/problem+json");
    response.body() = Json{{"type", "about:blank"},
                           {"title", "Access denied"},
                           {"status", response.result_int()},
                           {"code", authenticated.error().name()}}
                          .dump();
  } else if (request.method() != http::verb::get || request.target() != "/v1/feed/incidents") {
    response.result(http::status::not_found);
    response.body() = "{}";
  } else {
    auto feed = co_await gateway->snapshot(authenticated->tenant);
    if (!feed) {
      response.result(http::status::service_unavailable);
      response.body() = "{}";
    } else {
      response.body() = p::stream::json(*feed);
    }
  }
  response.prepare_payload();
  co_await http::async_write(socket.next_layer(), response, asio::as_tuple(asio::use_awaitable));
  stop();
}
}  // namespace psim::api_gateway

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "api-gateway",
                                       .settings_schema = std::string(psim::platform::stream::kSettings),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::api_gateway::Gateway>(context));
                                         return result;
                                       }});
}
