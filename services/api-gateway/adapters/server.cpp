#include <boost/asio/as_tuple.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <psim/api/v1/feed.pb.h>
#include <psim/api/v1/me.pb.h>
#include <psim/audit/v1/audit.pb.h>

#include <algorithm>
#include <atomic>
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
#include "psim/platform/authz/policy.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/redis/patterns.hpp"
#include "psim/platform/redis/pool.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/redis.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/realtime/v1/realtime.pb.h"
#include "realtime.hpp"
#include "routes.hpp"
#include "workers.hpp"

namespace psim::api_gateway {
namespace p = platform;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ws = beast::websocket;
using Tcp = asio::ip::tcp;
using Json = nlohmann::json;
class Gateway;

struct Subscription {
  realtime::v1::Subscribe request;
  p::kafka::Offsets positions;
  bool replaying = true;
  std::string digest{};
  std::uint64_t serial = 0;
  std::string request_id{};
};

struct ReplayBatch {
  p::kafka::Offsets begin;
  p::Result<p::kafka::Offsets> outcome = p::fail(p::ErrorCode::kCommonUnavailable, "Kafka replay pending");
  std::vector<RealtimeEvent> events;
  std::vector<std::weak_ptr<asio::steady_timer>> waiters;
  bool complete = false;
  std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
};

struct ScopeLabels {
  std::string site, zone;
  std::chrono::steady_clock::time_point confirmed;
};

class SessionBase {
 public:
  virtual ~SessionBase() = default;
  virtual void stop() = 0;
  virtual void send(const realtime::v1::ServerFrame&) = 0;
  virtual void fail(std::string code) = 0;
  asio::cancellation_signal cancel;
  std::optional<p::security::Principal> principal;
  std::string bearer;
  std::string access_context;
  std::map<std::string, Subscription> subscriptions;
  std::set<std::string> capabilities{"incident_event"};
  bool closed = false;
};

template <typename Stream>
class Session : public SessionBase, public std::enable_shared_from_this<Session<Stream>> {
 public:
  Session(Gateway* owner, Stream connection);
  asio::awaitable<void> run();
  asio::awaitable<void> lifecycle();
  asio::awaitable<void> write();
  void send(const realtime::v1::ServerFrame& frame) override;
  void stop() override;
  void fail(std::string code) override;
  Gateway* gateway;
  ws::stream<Stream> socket;
  asio::steady_timer wake;
  std::deque<std::string> queue;
  RateLimiter control{100, 200};
  std::size_t queued_bytes = 0;
  bool upgraded = false, writing = false, closing = false;
  const std::chrono::steady_clock::time_point opened = std::chrono::steady_clock::now();
};

class Gateway final : public p::stream::Worker {
 public:
  explicit Gateway(const p::runtime::Context& context)
      : p::stream::Worker(context, "realtime", ""),
        acceptor_(executor()),
        idle_(executor()),
        relay_timer_(executor()),
        epoch_(p::stream::uuid7()),
        state_timer_(executor()) {}

  bool ready() const override { return p::stream::Worker::ready() && healthy_.load(); }

  asio::awaitable<p::Result<void>> initialize() override {
    auto registered = co_await schema("psim.audit.v1");
    if (!registered) {
      co_return registered;
    }
    http_metrics_ = std::make_unique<HttpMetrics>(context());
    const auto& config = context().config();
    origins_ = config.get<std::vector<std::string>>("settings.allowed_origins");
    max_connections_ = config.get<std::size_t>("settings.realtime_max_connections");
    max_subscriptions_ = config.get<std::size_t>("settings.realtime_max_subscriptions");
    queue_messages_ = config.get<std::size_t>("settings.realtime_queue_messages");
    queue_bytes_ = config.get<std::size_t>("settings.realtime_queue_bytes");
    total_queue_bytes_ = config.get<std::size_t>("settings.realtime_total_queue_bytes");
    rate_ = config.get<std::int64_t>("settings.rate_per_second");
    burst_ = config.get<std::int64_t>("settings.rate_burst");
    auto redis = p::stream::redis_options(config);
    redis.acquire_timeout = std::chrono::milliseconds(500);
    redis.connection.connect_timeout = std::chrono::milliseconds(500);
    redis.connection.command_timeout = std::chrono::milliseconds(500);
    redis_ = std::make_unique<p::redis::Pool>(std::move(redis));
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
    auto authentication = p::stream::authentication_options(context());
    if (!authentication) {
      co_return std::unexpected(authentication.error());
    }
    auto verifier = p::security::JwksCache::create(std::move(*authentication));
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
    active_connections_ = &prometheus::BuildGauge()
                               .Name("psim_realtime_connections")
                               .Help("Active realtime connections")
                               .Register(context().metrics().registry())
                               .Add({});
    slow_consumers_ = &prometheus::BuildCounter()
                           .Name("psim_realtime_slow_consumers_total")
                           .Help("Realtime queue or write deadline failures")
                           .Register(context().metrics().registry())
                           .Add({});
    delivery_ =
        &prometheus::BuildHistogram()
             .Name("psim_realtime_delivery_seconds")
             .Help("Kafka timestamp to queued live delta")
             .Register(context().metrics().registry())
             .Add({}, prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.2, 0.5, 1});
    auto source = p::kafka::Consumer::create(executor(),
                                             {.client = kafka_options("realtime"),
                                              .group_id = "realtime",
                                              .topics = {kIncidentTopic, kResponseTopic},
                                              .manual = true},
                                             {.blocking = &context().blocking(), .logger = &context().logger()});
    if (!source) {
      co_return std::unexpected(source.error());
    }
    source_ = std::move(*source);
    auto bounds = co_await source_->offset_bounds(std::chrono::seconds(10));
    if (!bounds) {
      co_return std::unexpected(bounds.error());
    }
    heads_ = bounds->end;
    floors_ = heads_;
    auto assigned = source_->assign(heads_);
    if (!assigned) {
      co_return assigned;
    }
    healthy_.store(true);
    live_running_ = true;
    asio::co_spawn(executor(), live(), [this](const std::exception_ptr&) {
      healthy_.store(false);
      if (!stopping_) {
        context().logger().warn("Realtime Kafka delivery stopped; connections will be closed");
      }
      live_running_ = false;
      idle_.cancel();
    });
    state_running_ = true;
    asio::co_spawn(executor(), states(), [this](const std::exception_ptr&) {
      healthy_.store(false);
      if (!stopping_) {
        context().logger().warn("Realtime state delivery stopped; connections will be closed");
      }
      state_running_ = false;
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

  static void resync(SessionBase* session, const std::string& id, std::string reason) {
    realtime::v1::ServerFrame frame;
    frame.mutable_resync_required()->set_subscription_id(id);
    frame.mutable_resync_required()->set_reason(std::move(reason));
    session->send(frame);
    session->subscriptions.erase(id);
  }

  asio::awaitable<void> deliver(SessionBase* session, std::string id, RealtimeEvent event) {
    auto found = session->subscriptions.find(id);
    if (session->closed || !session->principal || found == session->subscriptions.end()) {
      co_return;
    }
    auto& sub = found->second;
    const auto serial = sub.serial;
    if (event.offset < sub.positions[event.partition]) {
      co_return;
    }
    if (session->principal->expires_at <= std::chrono::system_clock::now()) {
      session->stop();
      co_return;
    }
    if (event.tenant != session->principal->tenant) {
      sub.positions[event.partition] = event.offset + 1;
      co_return;
    }
    if (event.value.has_response_event() && event.site.empty() && !event.incident.empty()
        && session->capabilities.contains("response_event")) {
      if (!p::stream::uuid(event.incident)) {
        co_return;
      }
      const auto key = scope_key(*session->principal) + event.incident;
      auto cached = labels_.find(key);
      if (cached != labels_.end()
          && std::chrono::steady_clock::now() - cached->second.confirmed < std::chrono::seconds(2)) {
        event.site = cached->second.site;
        event.zone = cached->second.zone;
      } else {
        auto result =
            co_await p::security::http_request("https://" + endpoint("incidents").host + ":"
                                                   + endpoint("incidents").port + "/v1/incidents/" + event.incident,
                                               upstream_options(session->bearer));
        found = session->subscriptions.find(id);
        if (session->closed || found == session->subscriptions.end() || found->second.serial != serial) {
          co_return;
        }
        if (!result || result->status >= 500) {
          resync(session, id, "incident authorization unavailable");
          co_return;
        }
        if (result->status != 200) {
          found->second.positions[event.partition] = event.offset + 1;
          co_return;
        }
        auto body = Json::parse(result->body, nullptr, false);
        if (body.is_discarded() || !body.contains("incident")) {
          resync(session, id, "invalid incident authorization response");
          co_return;
        }
        event.site = body["incident"].value("siteId", "");
        event.zone = body["incident"].value("zoneId", "");
        if (labels_.size() >= 512) {
          labels_.erase(labels_.begin());
        }
        labels_[key] = {event.site, event.zone, std::chrono::steady_clock::now()};
      }
    }
    found = session->subscriptions.find(id);
    if (found == session->subscriptions.end() || found->second.serial != serial) {
      co_return;
    }
    found->second.positions[event.partition] = event.offset + 1;
    const bool supported = event.value.has_incident_event()
                               ? session->capabilities.contains("incident_event")
                                     && (!event.value.incident_event().has_incident_operational_updated()
                                         || session->capabilities.contains("incident_operational_updated"))
                               : event.value.has_response_event() && session->capabilities.contains("response_event");
    const bool response_access =
        !event.value.has_response_event()
        || p::authz::decide_role(
               *session->principal,
               event.value.response_event().has_site_duty_changed() ? "response.duty.read" : "response.run.read",
               std::chrono::system_clock::now())
               .has_value();
    if (!supported || !response_access || !realtime_filter(*session->principal, found->second.request, event)
        || session->principal->expires_at <= std::chrono::system_clock::now()) {
      co_return;
    }
    realtime::v1::ServerFrame frame;
    *frame.mutable_delta() = event.value;
    frame.mutable_delta()->set_subscription_id(id);
    frame.mutable_delta()->set_resume_token(
        resume_token(found->second.positions, session->principal->tenant, session->access_context));
    session->send(frame);
  }

  p::security::HttpOptions upstream_options(const std::string& bearer) const {
    const auto& config = context().config();
    return {.tls = {.ca_file = config.get<std::string>("settings.upstream_ca_file"),
                    .certificate_file = config.get<std::string>("settings.upstream_certificate_file"),
                    .private_key_file = config.get<std::string>("settings.upstream_key_file"),
                    .require_client_certificate = false},
            .timeout = std::chrono::milliseconds(1000),
            .max_body_bytes = 4194304,
            .bearer_token = bearer};
  }

  asio::awaitable<void> fetch_replay(std::shared_ptr<ReplayBatch> batch) {
    auto source = p::kafka::Consumer::create(executor(),
                                             {.client = kafka_options("realtime-replay"),
                                              .group_id = "realtime-replay",
                                              .topics = {kIncidentTopic, kResponseTopic},
                                              .manual = true},
                                             {.blocking = &context().blocking(), .logger = &context().logger()});
    if (!source) {
      batch->outcome = std::unexpected(source.error());
      co_return;
    }
    batch->outcome = co_await replay_events(source->get(), batch->begin,
                                            [batch](const RealtimeEvent& event) { batch->events.push_back(event); });
    co_await (*source)->close();
  }

  std::shared_ptr<ReplayBatch> batch_for(const p::kafka::Offsets& positions) {
    for (const auto& batch : batches_) {
      if (batch->begin == positions
          && (!batch->complete || std::chrono::steady_clock::now() - batch->created < std::chrono::seconds(2))) {
        return batch;
      }
    }
    std::erase_if(batches_, [](const auto& batch) { return batch->complete; });
    if (replays_ >= 4) {
      return {};
    }
    auto batch = std::make_shared<ReplayBatch>();
    batch->begin = positions;
    batches_.push_back(batch);
    ++replays_;
    asio::co_spawn(executor(), fetch_replay(batch), [this, batch](const std::exception_ptr& error) {
      if (error) {
        batch->outcome = p::fail(p::ErrorCode::kCommonUnavailable, "Kafka replay interrupted");
      }
      batch->complete = true;
      batch->created = std::chrono::steady_clock::now();
      for (const auto& weak : batch->waiters) {
        if (auto timer = weak.lock()) {
          timer->cancel();
        }
      }
      --replays_;
      idle_.cancel();
    });
    return batch;
  }

  asio::awaitable<void> subscribe(SessionBase* session, realtime::v1::Subscribe request, std::string request_id) {
    const auto id = request.subscription_id();
    if (!session->principal || id.empty() || id.size() > 128 || session->subscriptions.size() >= max_subscriptions_) {
      resync(session, id, "subscription limit or invalid identity");
      co_return;
    }
    const auto cursor = Json::parse(request.resume_token(), nullptr, false);
    if (cursor.is_object() && cursor.contains("accessContext")
        && (!cursor["accessContext"].is_string()
            || (!cursor["accessContext"].get_ref<const std::string&>().empty()
                && cursor["accessContext"] != session->access_context))) {
      resync(session, id, "access scope changed");
      co_return;
    }
    const auto& filter = request.filter();
    bool valid = filter.site_ids_size() <= 64 && filter.zone_ids_size() <= 64;
    for (const auto& site : filter.site_ids()) {
      valid = valid && p::stream::uuid(site);
    }
    for (const auto& zone : filter.zone_ids()) {
      valid = valid && p::stream::uuid(zone);
    }
    if (request.stream() == realtime::v1::STREAM_LOCATION_STATUS) {
      const auto token = Json::parse(request.resume_token(), nullptr, false);
      valid = valid && session->capabilities.contains("location_status") && p::stream::uuid(filter.location_id())
              && filter.site_ids().empty() && filter.zone_ids().empty() && filter.incident_id().empty()
              && token.is_object() && token.value("tenant", "") == session->principal->tenant
              && token.value("locationId", "") == filter.location_id();
      if (!valid) {
        resync(session, id, "unsupported location subscription");
        co_return;
      }
      session->subscriptions[id] = {.request = std::move(request),
                                    .positions = {},
                                    .digest = {},
                                    .serial = ++serial_,
                                    .request_id = std::move(request_id)};
      state_timer_.cancel();
      co_return;
    }
    valid = valid
            && (session->capabilities.contains("incident_event") || session->capabilities.contains("response_event"))
            && (request.stream() == realtime::v1::STREAM_INCIDENTS || request.stream() == realtime::v1::STREAM_INCIDENT)
            && filter.location_id().empty()
            && (request.stream() == realtime::v1::STREAM_INCIDENT ? p::stream::uuid(filter.incident_id())
                                                                  : filter.incident_id().empty());
    auto positions = resume_positions(request.resume_token(), session->principal->tenant);
    if (!valid || !positions) {
      resync(session, id, "snapshot expired or invalid filter");
      co_return;
    }
    session->subscriptions[id] = {.request = std::move(request),
                                  .positions = *positions,
                                  .digest = {},
                                  .serial = ++serial_,
                                  .request_id = request_id};
    bool available = positions->size() == heads_.size();
    for (const auto& [partition, head] : heads_) {
      available = available && positions->contains(partition) && positions->at(partition) >= floors_.at(partition)
                  && positions->at(partition) <= head;
    }
    std::shared_ptr<ReplayBatch> batch;
    if (!available) {
      batch = batch_for(*positions);
      if (!batch) {
        resync(session, id, "replay capacity reached");
        co_return;
      }
      if (!batch->complete) {
        auto waiter = std::make_shared<asio::steady_timer>(executor());
        waiter->expires_after(std::chrono::seconds(12));
        batch->waiters.push_back(waiter);
        co_await waiter->async_wait(asio::as_tuple(asio::use_awaitable));
      }
      if (session->closed) {
        co_return;
      }
      if (!batch->complete || !batch->outcome) {
        resync(session, id, "Kafka replay requires a fresh snapshot");
        co_return;
      }
    }
    realtime::v1::ServerFrame frame;
    frame.set_request_id(std::move(request_id));
    frame.mutable_subscribed()->set_subscription_id(id);
    frame.mutable_subscribed()->set_resume_token(session->subscriptions.at(id).request.resume_token());
    session->send(frame);
    if (batch) {
      for (const auto& event : batch->events) {
        if (session->closed || !session->subscriptions.contains(id)) {
          co_return;
        }
        co_await deliver(session, id, event);
        co_await asio::post(executor(), asio::use_awaitable);
      }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      auto found = session->subscriptions.find(id);
      if (session->closed || found == session->subscriptions.end()) {
        co_return;
      }
      for (const auto& [partition, floor] : floors_) {
        if (found->second.positions[partition] < floor) {
          resync(session, id, "live journal changed during replay");
          co_return;
        }
      }
      const auto captured = heads_;
      const auto pending = journal_;
      for (const auto& event : pending) {
        co_await deliver(session, id, *event);
        co_await asio::post(executor(), asio::use_awaitable);
      }
      found = session->subscriptions.find(id);
      if (session->closed || found == session->subscriptions.end()) {
        co_return;
      }
      if (captured == heads_) {
        for (const auto& [partition, offset] : heads_) {
          found->second.positions[partition] = std::max(found->second.positions[partition], offset);
        }
        found->second.replaying = false;
        break;
      }
      if (std::chrono::steady_clock::now() > deadline) {
        resync(session, id, "live catch-up budget reached");
        co_return;
      }
    }
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* /*message*/, p::kafka::Output* /*output*/) override {
    co_return p::Result<void>{};
  }

  asio::awaitable<void> live() {
    while (!stopping_) {
      auto messages = co_await source_->poll(64, std::chrono::milliseconds(2));
      if (!messages) {
        co_return;
      }
      for (const auto& message : *messages) {
        auto trace = p::stream::span(context(), "deliver incident", message.headers());
        auto event = realtime_event(message);
        if (!event) {
          co_return;
        }
        heads_[event->partition] = event->offset + 1;
        journal_bytes_ += event->value.ByteSizeLong() + 256;
        journal_.push_back(std::make_shared<const RealtimeEvent>(*event));
        while (!journal_.empty()
               && (journal_.size() > 10000 || journal_bytes_ > 8388608
                   || journal_.front()->timestamp < std::chrono::system_clock::now() - std::chrono::minutes(15))) {
          const auto& old = *journal_.front();
          floors_[old.partition] = old.offset + 1;
          journal_bytes_ -= old.value.ByteSizeLong() + 256;
          journal_.pop_front();
        }
        const std::vector<std::shared_ptr<SessionBase>> sessions(sessions_.begin(), sessions_.end());
        std::size_t deliveries = 0;
        for (const auto& session : sessions) {
          std::vector<std::string> ids;
          for (const auto& [id, sub] : session->subscriptions) {
            if (!sub.replaying && sub.request.stream() != realtime::v1::STREAM_LOCATION_STATUS) {
              ids.push_back(id);
            }
          }
          for (const auto& id : ids) {
            co_await deliver(session.get(), id, *event);
            if (++deliveries % 32 == 0) {
              co_await asio::post(executor(), asio::use_awaitable);
            }
          }
        }
        co_await asio::post(executor(), asio::use_awaitable);
        delivery_->Observe(
            std::max(0.0, std::chrono::duration<double>(std::chrono::system_clock::now() - event->timestamp).count()));
        trace->End();
      }
      heads_ = source_->positions();
    }
  }

  asio::awaitable<void> states() {
    while (!stopping_) {
      using Member = std::pair<std::shared_ptr<SessionBase>, std::string>;
      std::map<std::string, std::vector<Member>> groups;
      for (const auto& session : sessions_) {
        const auto principal = session->principal;
        if (principal && !session->closed && principal->expires_at > std::chrono::system_clock::now()) {
          for (const auto& [id, sub] : session->subscriptions) {
            if (sub.request.stream() == realtime::v1::STREAM_LOCATION_STATUS) {
              groups[scope_key(*principal) + sub.request.filter().location_id()].emplace_back(session, id);
            }
          }
        }
      }
      for (const auto& [key, members] : groups) {
        (void)key;
        if (stopping_) {
          break;
        }
        const auto& [first, id] = members.front();
        if (first->closed || !first->subscriptions.contains(id)) {
          continue;
        }
        const auto location = first->subscriptions.at(id).request.filter().location_id();
        auto result = co_await p::security::http_request("https://" + endpoint("projection").host + ":"
                                                             + endpoint("projection").port + "/v1/feed/locations/"
                                                             + location + "/status",
                                                         upstream_options(first->bearer));
        api::v1::GetLocationStatusResponse image;
        const bool valid =
            result && result->status == 200 && google::protobuf::util::JsonStringToMessage(result->body, &image).ok();
        std::string digest{};
        if (valid) {
          auto content = Json::parse(result->body);
          content.erase("snapshotAt");
          content.erase("resumeToken");
          for (auto& connector : content["connectors"]) {
            if (connector.value("connection", "") == "online") {
              connector.erase("confirmedAt");
            }
          }
          digest = p::stream::sha256(content.dump());
        }
        for (const auto& [session, identity] : members) {
          auto found = session->subscriptions.find(identity);
          if (session->closed || found == session->subscriptions.end() || !session->principal
              || scope_key(*session->principal) + found->second.request.filter().location_id() != key) {
            continue;
          }
          if (!valid) {
            resync(session.get(), identity, "location snapshot unavailable or outside access scope");
            continue;
          }
          if (session->principal->expires_at <= std::chrono::system_clock::now()) {
            session->stop();
            continue;
          }
          if (found->second.replaying) {
            realtime::v1::ServerFrame ack;
            ack.set_request_id(found->second.request_id);
            ack.mutable_subscribed()->set_subscription_id(identity);
            ack.mutable_subscribed()->set_resume_token(image.resume_token());
            session->send(ack);
            found->second.replaying = false;
          }
          if (found->second.digest != digest) {
            realtime::v1::ServerFrame frame;
            frame.mutable_delta()->set_subscription_id(identity);
            frame.mutable_delta()->set_resume_token(image.resume_token());
            *frame.mutable_delta()->mutable_location_status() = image;
            session->send(frame);
            found->second.digest = digest;
          }
        }
      }
      state_timer_.expires_after(std::chrono::milliseconds(50));
      co_await state_timer_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
  }

  asio::awaitable<void> accept() {
    while (accepting_) {
      auto [error, socket] = co_await acceptor_.async_accept(asio::as_tuple(asio::use_awaitable));
      if (error) {
        break;
      }
      if (!healthy_.load() || sessions_.size() >= max_connections_) {
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
    healthy_.store(false);
    stopping_ = true;
    state_timer_.cancel();
    accepting_ = false;
    boost::system::error_code ignored;
    (void)acceptor_.close(ignored);
    for (const auto& session : sessions_) {
      session->stop();
    }
    relay_timer_.cancel();
    while (!sessions_.empty() || listening_ || relaying_ || live_running_ || state_running_ || replays_ != 0) {
      idle_.expires_after(std::chrono::milliseconds(10));
      co_await idle_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    if (source_) {
      co_await source_->close();
      source_.reset();
    }
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
    verifier_.reset();
    if (redis_) {
      redis_->close();
      redis_.reset();
    }
  }

  template <typename Stream>
  void launch(std::shared_ptr<Session<Stream>> session) {
    sessions_.insert(session);
    asio::co_spawn(
        executor(), session->lifecycle(),
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

  asio::awaitable<p::Result<void>> limit(std::string tenant, std::string subject) {
    auto decision = co_await p::redis::TokenBucket(redis_.get(), {"api-gateway", std::move(tenant)})
                        .consume(std::move(subject), rate_, burst_);
    if (!decision) {
      http_metrics_->limit_failed();
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "shared rate limiter unavailable");
    }
    if (!decision->allowed) {
      http_metrics_->limited();
      co_return p::fail(p::ErrorCode::kCommonRateLimited, "request rate exceeded");
    }
    co_return p::Result<void>{};
  }

  const Endpoint& endpoint(const std::string& service) const { return endpoints_.at(service); }

  asio::ssl::context& upstream_tls() { return *upstream_tls_; }

  HttpMetrics& metrics() { return *http_metrics_; }

  bool reserve(std::size_t bytes) {
    if (bytes > total_queue_bytes_ - queued_bytes_) {
      return false;
    }
    queued_bytes_ += bytes;
    return true;
  }

  void release(std::size_t bytes) { queued_bytes_ -= bytes; }

  std::size_t queue_messages() const { return queue_messages_; }

  std::size_t queue_bytes() const { return queue_bytes_; }

  void connection(bool opened) {
    if (opened) {
      active_connections_->Increment();
    } else {
      active_connections_->Decrement();
    }
  }

  void slow() { slow_consumers_->Increment(); }

  void trace_close(std::string reason) {
    context().logger().debug("Realtime connection closed", {{"reason", std::move(reason)}});
  }

  const std::string& epoch() const { return epoch_; }

 private:
  Tcp::acceptor acceptor_;
  asio::steady_timer idle_, relay_timer_;
  bool plaintext_ = false, relaying_ = false;
  std::shared_ptr<asio::ssl::context> tls_, upstream_tls_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  std::unique_ptr<RateLimiter> ip_limiter_;
  std::unique_ptr<p::redis::Pool> redis_;
  std::int64_t rate_ = 0, burst_ = 0;
  std::unique_ptr<HttpMetrics> http_metrics_;
  std::vector<std::string> origins_;
  std::map<std::string, Endpoint> endpoints_;
  bool accepting_ = false;
  bool listening_ = false;
  std::string epoch_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::unique_ptr<p::kafka::Consumer> source_;
  p::kafka::Offsets heads_, floors_;
  std::size_t journal_bytes_ = 0, replays_ = 0, queued_bytes_ = 0;
  bool stopping_ = false, live_running_ = false, state_running_ = false;
  asio::steady_timer state_timer_;
  prometheus::Gauge* active_connections_ = nullptr;
  prometheus::Counter* slow_consumers_ = nullptr;
  prometheus::Histogram* delivery_ = nullptr;
  std::deque<std::shared_ptr<const RealtimeEvent>> journal_;
  std::vector<std::shared_ptr<ReplayBatch>> batches_;
  std::map<std::string, ScopeLabels> labels_;
  std::uint64_t serial_ = 0;
  std::size_t max_connections_ = 1024, max_subscriptions_ = 16, queue_messages_ = 1000, queue_bytes_ = 4194304,
              total_queue_bytes_ = 67108864;
  std::atomic_bool healthy_{false};
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
  gateway->release(queued_bytes);
  queue.clear();
  queued_bytes = 0;
  cancel.emit(asio::cancellation_type::terminal);
  wake.cancel();
  boost::system::error_code ignored;
  (void)beast::get_lowest_layer(socket).socket().cancel(ignored);
  (void)beast::get_lowest_layer(socket).socket().close(ignored);
}

template <typename Stream>
void Session<Stream>::send(const realtime::v1::ServerFrame& frame) {
  if (closed || closing) {
    return;
  }
  // The writer refreshes authorization before transmission, including frames queued during that refresh.
  auto text = p::stream::json(frame);
  if (queue.size() >= gateway->queue_messages() || queued_bytes + text.size() > gateway->queue_bytes()
      || !gateway->reserve(text.size())) {
    gateway->slow();
    fail("REALTIME_SLOW_CONSUMER");
    return;
  }
  queued_bytes += text.size();
  queue.push_back(std::move(text));
  wake.cancel();
}

template <typename Stream>
void Session<Stream>::fail(std::string code) {
  if (closing || closed) {
    return;
  }
  realtime::v1::ServerFrame frame;
  frame.mutable_error()->set_code(std::move(code));
  frame.mutable_error()->set_fatal(true);
  frame.mutable_error()->set_message("Reconnect using the last confirmed resume token");
  gateway->release(queued_bytes);
  queue.clear();
  queued_bytes = 0;
  queue.push_back(p::stream::json(frame));
  queued_bytes = queue.back().size();
  if (!gateway->reserve(queued_bytes)) {
    queued_bytes = 0;
    queue.clear();
    stop();
    return;
  }
  closing = true;
  wake.cancel();
}

template <typename Stream>
asio::awaitable<void> Session<Stream>::write() {
  while (!closed) {
    if (!closing && !gateway->ready()) {
      gateway->trace_close("gateway-not-ready");
      stop();
      break;
    }
    if (!closing && principal && principal->expires_at <= std::chrono::system_clock::now()) {
      gateway->trace_close("token-expired");
      stop();
      break;
    }
    if (!closing && principal
        && std::chrono::system_clock::now() >= principal->checked_until - std::chrono::milliseconds(400)) {
      const auto checked_bearer = bearer;
      auto refreshed = co_await gateway->authenticate(checked_bearer);
      if (closed) {
        break;
      }
      if (checked_bearer != bearer) {
        continue;
      }
      if (!refreshed || refreshed->checked_until <= std::chrono::system_clock::now()
          || scope_key(*principal) != scope_key(*refreshed)) {
        if (!refreshed) {
          gateway->trace_close("authorization-refresh-failed:" + std::string(refreshed.error().name()) + ":"
                               + refreshed.error().message());
        } else if (refreshed->checked_until <= std::chrono::system_clock::now()) {
          gateway->trace_close("authorization-refresh-expired");
        } else {
          gateway->trace_close("authorization-scope-changed");
        }
        stop();
        break;
      }
      principal = std::move(*refreshed);
    }
    if (!principal && std::chrono::steady_clock::now() - opened > std::chrono::seconds(5)) {
      stop();
      break;
    }
    if (queue.empty()) {
      if (closing) {
        co_await socket.async_close(ws::close_reason(4008),
                                    asio::cancel_after(std::chrono::seconds(1), asio::as_tuple(asio::use_awaitable)));
        stop();
        break;
      }
      wake.expires_after(std::chrono::milliseconds(100));
      co_await wake.async_wait(asio::as_tuple(asio::use_awaitable));
      continue;
    }
    auto text = std::move(queue.front());
    queue.pop_front();
    queued_bytes -= text.size();
    const auto [error, size] = co_await socket.async_write(
        asio::buffer(text), asio::cancel_after(std::chrono::seconds(2), asio::as_tuple(asio::use_awaitable)));
    (void)size;
    gateway->release(text.size());
    if (error) {
      if (!closed) {
        gateway->trace_close(error == asio::error::operation_aborted ? "write-timeout" : "write-error");
      }
      if (error == asio::error::operation_aborted && !closed) {
        gateway->slow();
      }
      stop();
      break;
    }
  }
}

template <typename Stream>
asio::awaitable<void> Session<Stream>::lifecycle() {
  try {
    co_await run();
  } catch (const std::exception&) {
    stop();
  }
  stop();
  co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
  while (writing) {
    wake.expires_after(std::chrono::milliseconds(10));
    co_await wake.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  if (upgraded) {
    gateway->connection(false);
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
    ws::permessage_deflate compression;
    compression.server_enable = true;
    compression.server_no_context_takeover = true;
    compression.client_no_context_takeover = true;
    socket.set_option(compression);
    socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::server));
    socket.read_message_max(16384);
    beast::get_lowest_layer(socket).expires_never();
    const auto [accepted] = co_await socket.async_accept(upgrade, asio::as_tuple(asio::use_awaitable));
    if (accepted) {
      stop();
      co_return;
    }
    upgraded = true;
    gateway->connection(true);
    writing = true;
    asio::co_spawn(socket.get_executor(), write(), [self = this->shared_from_this()](const std::exception_ptr&) {
      self->writing = false;
      self->wake.cancel();
      self->stop();
    });
    while (!closed) {
      buffer.consume(buffer.size());
      const auto [read_error, read_bytes] = co_await socket.async_read(buffer, asio::as_tuple(asio::use_awaitable));
      (void)read_bytes;
      if (read_error) {
        break;
      }
      if (!socket.got_text() || !control.allow("frames", std::chrono::steady_clock::now())) {
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
          fail(authenticated ? "AUTH_FORBIDDEN" : std::string(authenticated.error().name()));
          asio::steady_timer flush(socket.get_executor());
          const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
          while (!closed && std::chrono::steady_clock::now() < deadline) {
            flush.expires_after(std::chrono::milliseconds(10));
            co_await flush.async_wait(asio::as_tuple(asio::use_awaitable));
          }
          break;
        }
        std::set<std::string> negotiated;
        for (const auto& type : frame.authenticate().supported_change_types()) {
          if (type == "incident_event" || type == "response_event" || type == "location_status"
              || type == "incident_operational_updated") {
            negotiated.insert(type);
          }
        }
        if (frame.authenticate().supported_change_types().empty()) {
          negotiated.insert("incident_event");
        }
        const auto next_context = p::stream::sha256(p::security::authorization_context(*authenticated));
        if (principal && (access_context != next_context || negotiated != capabilities)) {
          gateway->release(queued_bytes);
          queue.clear();
          queued_bytes = 0;
          std::vector<std::string> ids;
          for (const auto& [id, sub] : subscriptions) {
            (void)sub;
            ids.push_back(id);
          }
          for (const auto& id : ids) {
            Gateway::resync(this, id, "authentication scope or capabilities changed");
          }
        }
        principal = std::move(*authenticated);
        access_context = next_context;
        bearer = frame.authenticate().access_token();
        capabilities = std::move(negotiated);
        if (capabilities.contains("incident_event") || capabilities.contains("response_event")) {
          response.mutable_authenticated()->add_supported_streams(realtime::v1::STREAM_INCIDENTS);
          response.mutable_authenticated()->add_supported_streams(realtime::v1::STREAM_INCIDENT);
        }
        if (capabilities.contains("location_status")) {
          response.mutable_authenticated()->add_supported_streams(realtime::v1::STREAM_LOCATION_STATUS);
        }
        for (const auto& type : capabilities) {
          response.mutable_authenticated()->add_supported_change_types(type);
        }
        response.mutable_authenticated()->set_user_id(principal->subject);
        response.mutable_authenticated()->set_expires_in(static_cast<std::uint32_t>(std::max<std::int64_t>(
            0,
            std::chrono::duration_cast<std::chrono::seconds>(principal->expires_at - std::chrono::system_clock::now())
                .count())));
        send(response);
      } else if (frame.has_subscribe()) {
        co_await gateway->subscribe(this, frame.subscribe(), frame.request_id());
      } else if (frame.has_unsubscribe()) {
        subscriptions.erase(frame.unsubscribe().subscription_id());
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
    p::Result<void> limited;
    if (access) {
      limited = co_await gateway->limit(authenticated->tenant, authenticated->subject);
    }
    if (!access) {
      auto logged = co_await gateway->denied(authenticated ? std::optional(*authenticated) : std::nullopt,
                                             (*selected)->operation);
      co_await problem(logged ? access.error() : logged.error());
    } else if (!limited) {
      co_await problem(limited.error());
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
        const auto scope = p::authz::scope_filter(*authenticated);
        for (const auto& site : scope.sites) {
          me.add_site_ids(site);
        }
        for (const auto& zone : scope.zones) {
          me.add_zone_ids(zone);
        }
        if (scope.all) {
          me.add_site_ids("*");
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
std::unique_ptr<platform::runtime::Component> gateway(const platform::runtime::Context& context) {
  return std::make_unique<Gateway>(context);
}
}  // namespace psim::api_gateway
