#include "workers.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <google/protobuf/util/time_util.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "http.hpp"
#include "psim/audit/v1/audit.pb.h"
#include "psim/common/v1/envelope.pb.h"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/replica.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/processing/v1/signal.pb.h"
#include "retention.hpp"
#include "search.hpp"

namespace psim::event_history {
namespace asio = boost::asio;
namespace beast = boost::beast;
using Tcp = asio::ip::tcp;

std::unique_ptr<p::clickhouse::Pool> history_connection(const p::runtime::Context& context, bool reader) {
  const auto& c = context.config();
  auto user = c.get<std::string>("settings.clickhouse_user");
  auto password = c.get<std::string>("settings.clickhouse_password_file");
  if (reader && !c.get<std::string>("settings.history_read_user").empty()) {
    user = c.get<std::string>("settings.history_read_user");
    password = c.get<std::string>("settings.history_read_password_file");
  }
  return std::make_unique<p::clickhouse::Pool>(
      &context.blocking(), p::clickhouse::PoolOptions{
                               .endpoints = {{c.get<std::string>("settings.clickhouse_host"),
                                              c.get<std::uint16_t>("settings.clickhouse_port")}},
                               .user = user,
                               .password_file = password,
                               .tls = {.ca_file = c.get<std::string>("settings.clickhouse_ca_file"),
                                       .certificate_file = c.get<std::string>("settings.clickhouse_certificate_file"),
                                       .private_key_file = c.get<std::string>("settings.clickhouse_private_key_file")},
                               .allow_plaintext = c.get<bool>("settings.allow_plaintext"),
                               .query_timeout = std::chrono::milliseconds(5000)});
}

class RetentionWorker final : public p::stream::Worker {
 public:
  explicit RetentionWorker(const p::runtime::Context& c)
      : p::stream::Worker(c, "history-retention", "psim.signals.v1", p::kafka::Semantics::kAtLeastOnce),
        timer_(executor()),
        done_(executor()) {}

  bool ready() const override { return p::stream::Worker::ready() && !failed_.load(); }

  std::string consumer_group() const override { return "psim-history-retention"; }

  asio::awaitable<p::Result<void>> initialize() override {
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    auto initialized = co_await initialize_history(pool_.get());
    if (!initialized) {
      co_return initialized;
    }
    destination_ = history_connection(context());
    retention_ = std::make_unique<Retention>(pool_.get(), destination_.get());
    auto control = co_await p::stream::Replica::open(executor(), kafka_options("retention-control"),
                                                     {.blocking = &context().blocking(), .logger = &context().logger()},
                                                     {"psim.config.v1"},
                                                     [this](const p::kafka::Message& message) -> p::Result<void> {
                                                       auto r = decode<config::v1::ConfigRecord>(message);
                                                       return r ? facts_.config(*r) : std::unexpected(r.error());
                                                     });
    if (!control) {
      co_return std::unexpected(control.error());
    }
    control_ = std::move(*control);
    running_ = true;
    asio::co_spawn(executor(), run(), [this](const std::exception_ptr& failure) {
      if (failure) {
        failed_.store(true);
        context().logger().error("history retention stopped");
      }
      running_ = false;
      done_.cancel();
    });
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* /*output*/) override {
    auto updated = co_await control_->update();
    if (!updated) {
      co_return updated;
    }
    auto r = decode<processing::v1::SignalRecord>(*message);
    if (!r) {
      co_return std::unexpected(r.error());
    }
    const auto seconds = facts_.retention(r->envelope().tenant_id());
    co_return co_await retention_->accept(std::move(*r), seconds);
  }

  asio::awaitable<void> run() {
    while (!stopping_) {
      auto result = co_await retention_->tick();
      if (!result) {
        context().logger().warn("history retention deferred", {{"code", std::string(result.error().name())}});
      }
      timer_.expires_after(std::chrono::milliseconds(result ? 100 : 500));
      if (!stopping_) {
        co_await timer_.async_wait(asio::as_tuple(asio::use_awaitable));
      }
    }
  }

  asio::awaitable<void> shutdown() override {
    stopping_ = true;
    timer_.cancel();
    while (running_) {
      done_.expires_at(asio::steady_timer::time_point::max());
      co_await done_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    if (control_) {
      co_await control_->close();
    }
    retention_.reset();
    if (destination_) {
      destination_->close();
    }
    if (pool_) {
      pool_->close();
    }
  }

 private:
  std::atomic_bool failed_{false};
  Facts facts_;
  asio::steady_timer timer_, done_;
  bool stopping_ = false, running_ = false;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<p::clickhouse::Pool> destination_;
  std::unique_ptr<p::stream::Replica> control_;
  std::unique_ptr<Retention> retention_;
};

class HttpWorker final : public p::stream::Worker {
 public:
  bool ready() const override { return p::stream::Worker::ready() && !failed_.load(); }

  explicit HttpWorker(const p::runtime::Context& c)
      : p::stream::Worker(c, "history-api", ""), acceptor_(executor()), timer_(executor()), done_(executor()) {}

  asio::awaitable<p::Result<void>> initialize() override {
    auto registered = co_await schema("psim.audit.v1");
    if (!registered) {
      co_return registered;
    }
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    auto initialized = co_await initialize_history(pool_.get());
    if (!initialized) {
      co_return initialized;
    }
    destination_ = history_connection(context(), true);
    search_ =
        std::make_unique<Search>(destination_.get(), [this](p::security::Principal principal, std::string action) {
          return denied(std::move(principal), std::move(action));
        });
    api_ = std::make_unique<Api>(search_.get());
    const auto& config = context().config();
    auto tls = co_await context().blocking().run([&config] {
      const auto dir = config.get<std::string>("settings.tls_directory");
      const auto cert = config.get<std::string>("settings.tls_certificate_file");
      const auto key = config.get<std::string>("settings.tls_private_key_file");
      return p::security::tls_context({.boundary = p::security::Boundary::kExternal,
                                       .ca_file = config.get<std::string>("settings.tls_ca_file"),
                                       .certificate_file = cert.empty() ? dir + "/history-server.pem" : cert,
                                       .private_key_file = key.empty() ? dir + "/history-server.key" : key,
                                       .require_client_certificate = false},
                                      true);
    });
    if (!tls) {
      co_return std::unexpected(tls.error());
    }
    tls_ = std::move(*tls);
    auto authentication = p::stream::authentication_options(context());
    if (!authentication) {
      co_return std::unexpected(authentication.error());
    }
    auto verifier = p::security::JwksCache::create(std::move(*authentication));
    if (!verifier) {
      co_return std::unexpected(verifier.error());
    }
    verifier_ = std::move(*verifier);
    auto refreshed = co_await verifier_->refresh();
    if (!refreshed) {
      co_return refreshed;
    }
    relay_ = std::make_unique<p::pg::OutboxRelay>(pool_.get(), p::pg::MessageStore("history"),
                                                  p::pg::kafka_publisher(producer()));
    acceptor_.open(Tcp::v4());
    acceptor_.set_option(Tcp::acceptor::reuse_address(true));
    acceptor_.bind({Tcp::v4(), static_cast<unsigned short>(config.get<int>("settings.listen_port"))});
    acceptor_.listen(64);
    launch(accept());
    launch(publish());
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* /*message*/, p::kafka::Output* /*output*/) override {
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> denied(p::security::Principal principal, std::string action) {
    audit::v1::AuditEventRecord event;
    auto* e = event.mutable_envelope();
    e->set_message_id(p::stream::uuid7());
    e->set_tenant_id(principal.tenant);
    e->set_sequence(1);
    e->set_correlation_id(e->message_id());
    e->set_schema_version("1");
    *e->mutable_occurred_at() = google::protobuf::util::TimeUtil::GetCurrentTime();
    *e->mutable_produced_at() = e->occurred_at();
    e->mutable_actor()->set_kind(common::v1::ACTOR_KIND_USER);
    e->mutable_actor()->set_id(principal.subject);
    auto* a = event.mutable_audit_entry();
    *a->mutable_actor() = e->actor();
    a->set_action("access.denied");
    a->set_object_type("event_history");
    a->set_outcome(audit::v1::AUDIT_OUTCOME_DENIED);
    (*a->mutable_details())["attempted_action"] = std::move(action);
    co_return co_await p::pg::in_transaction<void>(
        pool_.get(),
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
        [this, &event](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          co_return co_await p::pg::MessageStore("history").append(
              tx, {event.envelope().message_id(), record("psim.audit.v1", event.envelope().tenant_id(), event, {})});
        });
  }

  void launch(asio::awaitable<void> operation) {
    ++background_;
    asio::co_spawn(executor(), std::move(operation), [this](const std::exception_ptr& failure) {
      if (failure) {
        failed_.store(true);
        context().logger().error("history HTTP background operation failed");
      }
      --background_;
      done_.cancel();
    });
  }

  asio::awaitable<void> publish() {
    while (!stopping_) {
      auto result = co_await relay_->drain_once();
      timer_.expires_after(std::chrono::milliseconds(result ? 10 : 250));
      if (!stopping_) {
        co_await timer_.async_wait(asio::as_tuple(asio::use_awaitable));
      }
    }
  }

  asio::awaitable<void> session(std::shared_ptr<beast::ssl_stream<beast::tcp_stream>> socket) {
    beast::get_lowest_layer(*socket).expires_after(std::chrono::seconds(5));
    auto [handshake] =
        co_await socket->async_handshake(asio::ssl::stream_base::server, asio::as_tuple(asio::use_awaitable));
    if (handshake) {
      co_return;
    }
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.header_limit(16384);
    parser.body_limit(std::size_t{1} * 1024 * 1024);
    beast::get_lowest_layer(*socket).expires_after(std::chrono::seconds(10));
    auto [header, bytes] =
        co_await http::async_read_header(*socket, buffer, parser, asio::as_tuple(asio::use_awaitable));
    (void)bytes;
    if (header) {
      co_return;
    }
    const std::string authorization(parser.get()[http::field::authorization]);
    p::Result<p::security::Principal> principal = p::fail(p::ErrorCode::kAuthUnauthenticated, "Bearer token required");
    if (authorization.starts_with("Bearer ") && authorization.size() <= 8192) {
      principal = co_await verifier_->verify(authorization.substr(7));
    }
    Response response;
    if (!principal) {
      if (principal.error().code() == p::ErrorCode::kCommonUnavailable) {
        context().logger().warn("history authentication unavailable",
                                {{"error", std::string(principal.error().message())}});
      }
      response = Api::problem(principal.error());
    } else {
      const std::size_t limit = 1048576;
      if (parser.content_length() && *parser.content_length() > limit) {
        response = Api::problem(p::Error(p::ErrorCode::kCommonInvalidArgument, "request body exceeds limit"));
      } else {
        parser.body_limit(limit);
        beast::get_lowest_layer(*socket).expires_after(std::chrono::seconds(30));
        auto [body, count] = co_await http::async_read(*socket, buffer, parser, asio::as_tuple(asio::use_awaitable));
        (void)count;
        if (body) {
          response = Api::problem(p::Error(p::ErrorCode::kCommonInvalidArgument, "invalid or oversized body"));
        } else {
          auto result = co_await api_->handle(parser.release(), std::move(*principal));
          if (!result) {
            context().logger().warn("history query failed", {{"code", std::string(result.error().name())},
                                                             {"error", std::string(result.error().message())}});
          }
          response = result ? std::move(*result) : Api::problem(result.error());
        }
      }
    }
    response.keep_alive(false);
    response.set(http::field::cache_control, "no-store");
    response.set(http::field::x_content_type_options, "nosniff");
    response.prepare_payload();
    beast::get_lowest_layer(*socket).expires_after(std::chrono::seconds(30));
    co_await http::async_write(*socket, response, asio::as_tuple(asio::use_awaitable));
  }

  asio::awaitable<void> accept() {
    while (!stopping_) {
      auto [error, connection] = co_await acceptor_.async_accept(asio::as_tuple(asio::use_awaitable));
      if (error) {
        break;
      }
      if (sessions_.size() >= 16) {
        connection.close();
        continue;
      }
      auto socket = std::make_shared<beast::ssl_stream<beast::tcp_stream>>(std::move(connection), *tls_);
      sessions_.insert(socket);
      asio::co_spawn(executor(), session(socket), [this, socket](const std::exception_ptr& failure) {
        if (failure) {
          context().logger().warn("history HTTP session failed");
        }
        boost::system::error_code ignored;
        (void)beast::get_lowest_layer(*socket).socket().close(ignored);
        sessions_.erase(socket);
        done_.cancel();
      });
    }
  }

  asio::awaitable<void> shutdown() override {
    stopping_ = true;
    boost::system::error_code ignored;
    (void)acceptor_.close(ignored);
    timer_.cancel();
    for (const auto& socket : sessions_) {
      (void)beast::get_lowest_layer(*socket).socket().close(ignored);
    }
    while (background_ != 0 || !sessions_.empty()) {
      done_.expires_at(asio::steady_timer::time_point::max());
      co_await done_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    api_.reset();
    search_.reset();
    relay_.reset();
    if (destination_) {
      destination_->close();
    }
    if (pool_) {
      pool_->close();
    }
  }

 private:
  std::atomic_bool failed_{false};
  Tcp::acceptor acceptor_;
  asio::steady_timer timer_, done_;
  bool stopping_ = false;
  std::size_t background_ = 0;
  std::set<std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>> sessions_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<p::clickhouse::Pool> destination_;
  std::unique_ptr<Search> search_;
  std::unique_ptr<Api> api_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::shared_ptr<asio::ssl::context> tls_;
};

std::unique_ptr<p::runtime::Component> history_http(const p::runtime::Context& context) {
  return std::make_unique<HttpWorker>(context);
}

std::unique_ptr<p::runtime::Component> history_retention(const p::runtime::Context& context) {
  return std::make_unique<RetentionWorker>(context);
}
}  // namespace psim::event_history
