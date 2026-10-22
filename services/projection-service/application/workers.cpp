#include "workers.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <google/protobuf/util/time_util.h>
#include <prometheus/histogram.h>
#include <psim/common/v1/envelope.pb.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <memory>
#include <set>
#include <utility>

#include "psim/audit/v1/audit.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/pg/repository.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/pg/transaction.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "report.hpp"

namespace psim::projection {
namespace asio = boost::asio;
namespace beast = boost::beast;
using Tcp = asio::ip::tcp;

namespace {
asio::awaitable<p::Result<void>> initialize_reports(p::pg::Pool* pool) {
  auto lease = co_await pool->acquire();
  if (!lease) {
    co_return std::unexpected(lease.error());
  }
  co_return co_await p::pg::MessageStore("reports").initialize(&**lease);
}
}  // namespace

class HttpWorker final : public p::stream::Worker {
 public:
  bool ready() const override { return p::stream::Worker::ready() && !failed_.load() && relay_healthy_.load(); }

  explicit HttpWorker(const p::runtime::Context& c)
      : p::stream::Worker(c, "reports-api", ""), acceptor_(executor()), timer_(executor()), done_(executor()) {}

  asio::awaitable<p::Result<void>> initialize() override {
    latency_ = &prometheus::BuildHistogram()
                    .Name("psim_report_query_seconds")
                    .Help("Incident report request latency")
                    .Register(context().metrics().registry())
                    .Add({}, prometheus::Histogram::BucketBoundaries{0.01, 0.05, 0.1, 0.2, 0.5, 1, 2, 3, 5});
    auto registered = co_await schema("psim.audit.v1");
    if (!registered) {
      co_return registered;
    }
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    auto initialized = co_await initialize_reports(pool_.get());
    if (!initialized) {
      co_return initialized;
    }
    destination_ = connection(context(), true);
    api_ = std::make_unique<Api>(destination_.get(),
                                 [this](p::security::Principal principal, std::string action, bool denied) {
                                   return audit(std::move(principal), std::move(action), denied);
                                 });
    const auto& config = context().config();
    auto tls = co_await context().blocking().run([&config] {
      const auto dir = config.get<std::string>("settings.tls_directory");
      const auto cert = config.get<std::string>("settings.tls_certificate_file");
      const auto key = config.get<std::string>("settings.tls_private_key_file");
      return p::security::tls_context({.boundary = p::security::Boundary::kExternal,
                                       .ca_file = config.get<std::string>("settings.tls_ca_file"),
                                       .certificate_file = cert.empty() ? dir + "/reports-server.pem" : cert,
                                       .private_key_file = key.empty() ? dir + "/reports-server.key" : key,
                                       .require_client_certificate = false},
                                      true);
    });
    if (!tls) {
      co_return std::unexpected(tls.error());
    }
    tls_ = std::move(*tls);
    auto verifier = p::security::JwksCache::create(
        {.jwt = {.issuer = config.get<std::string>("settings.issuer"), .audience = "psim"},
         .url = config.get<std::string>("settings.jwks_url"),
         .http = {
             .tls = {.ca_file = config.get<std::string>("settings.jwks_ca_file"), .require_client_certificate = false},
             .allow_plaintext = config.get<bool>("settings.allow_plaintext")}});
    if (!verifier) {
      co_return std::unexpected(verifier.error());
    }
    verifier_ = std::move(*verifier);
    auto refreshed = co_await verifier_->refresh();
    if (!refreshed) {
      co_return refreshed;
    }
    relay_ = std::make_unique<p::pg::OutboxRelay>(pool_.get(), p::pg::MessageStore("reports"),
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

  asio::awaitable<p::Result<void>> audit(p::security::Principal principal, std::string action, bool denied) {
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
    a->set_action(denied ? "access.denied" : action);
    a->set_object_type("incident_report");
    a->set_outcome(denied ? audit::v1::AUDIT_OUTCOME_DENIED : audit::v1::AUDIT_OUTCOME_SUCCESS);
    (*a->mutable_details())["attempted_action"] = std::move(action);
    co_return co_await p::pg::in_transaction<void>(
        pool_.get(),
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): awaited owned closure.
        [this, &event](p::pg::Transaction* tx) -> asio::awaitable<p::Result<void>> {
          co_return co_await p::pg::MessageStore("reports").append(
              tx, {event.envelope().message_id(), record("psim.audit.v1", event.envelope().tenant_id(), event, {})});
        });
  }

  void launch(asio::awaitable<void> operation) {
    ++background_;
    asio::co_spawn(executor(), std::move(operation), [this](const std::exception_ptr& failure) {
      if (failure) {
        failed_.store(true);
        context().logger().error("reports HTTP background operation failed");
      }
      --background_;
      done_.cancel();
    });
  }

  asio::awaitable<void> publish() {
    while (!stopping_) {
      auto result = co_await relay_->drain_once();
      relay_healthy_.store(result.has_value());
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
          auto trace = p::stream::span(context(), "report.query");
          const auto started = std::chrono::steady_clock::now();
          auto result = co_await api_->handle(parser.release(), std::move(*principal));
          latency_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
          trace->End();
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
          context().logger().warn("reports HTTP session failed");
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
    relay_.reset();
    if (destination_) {
      destination_->close();
    }
    if (pool_) {
      pool_->close();
    }
  }

 private:
  prometheus::Histogram* latency_ = nullptr;
  std::atomic_bool failed_{false}, relay_healthy_{true};
  Tcp::acceptor acceptor_;
  asio::steady_timer timer_, done_;
  bool stopping_ = false;
  std::size_t background_ = 0;
  std::set<std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>> sessions_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<p::clickhouse::Pool> destination_;
  std::unique_ptr<Api> api_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::shared_ptr<asio::ssl::context> tls_;
};

std::unique_ptr<p::runtime::Component> reports_http(const p::runtime::Context& context) {
  return std::make_unique<HttpWorker>(context);
}
}  // namespace psim::projection
