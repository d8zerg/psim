#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <psim/processing/v1/signal.pb.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "http.hpp"
#include "model.hpp"
#include "psim/catalog/v1/catalog.pb.h"
#include "psim/incident/v1/events.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/http.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/response/v1/command.pb.h"
#include "psim/response/v1/events.pb.h"
#include "repository.hpp"

namespace psim::response_engine {
namespace beast = boost::beast;
using Tcp = asio::ip::tcp;

namespace {
std::string settings() {
  auto schema = Json::parse(p::stream::kSettings);
  auto& fields = schema["properties"];
  for (const auto* name :
       {"tls_ca_file", "tls_certificate_file", "tls_private_key_file", "jwks_ca_file", "handler_ca_file"}) {
    fields[name] = {{"type", "string"}, {"default", ""}};
  }
  fields["handler_urls"] = {{"type", "array"}, {"items", {{"type", "string"}}}, {"default", Json::array()}};
  return schema.dump();
}

asio::awaitable<p::Result<bool>> invoke_handler(std::string ca, std::string url, Json body, int timeout) {
  auto result =
      co_await p::security::http_request(std::move(url),
                                         {.tls = {.ca_file = std::move(ca), .require_client_certificate = false},
                                          .timeout = std::chrono::seconds(timeout),
                                          .max_body_bytes = 65536},
                                         body.dump(), "application/json");
  if (!result) {
    co_return std::unexpected(result.error());
  }
  co_return result->status >= 200 && result->status < 300;
}

}  // namespace

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "response", "psim.incidents.events.v1", p::kafka::Semantics::kAtLeastOnce),
        acceptor_(executor()),
        tick_(executor()),
        relay_timer_(executor()),
        effect_timer_(executor()),
        done_(executor()) {}

  std::string consumer_group() const override { return "psim-response-engine"; }

  std::size_t batch_size() const override { return 1; }

  [[nodiscard]] bool ready() const override {
    return p::stream::Worker::ready() && healthy_.load() && tick_healthy_.load() && relay_healthy_.load();
  }

  asio::awaitable<p::Result<void>> initialize() override {
    for (const auto* topic : {"psim.response.events.v1", "psim.audit.v1"}) {
      auto registered = co_await schema(topic);
      if (!registered) {
        co_return registered;
      }
    }
    latency_ =
        &prometheus::BuildHistogram()
             .Name("psim_response_transaction_seconds")
             .Help("Response transaction latency")
             .Register(context().metrics().registry())
             .Add({}, prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01, 0.02, 0.03, 0.04, 0.05, 0.1, 0.5, 1});
    ignored_ = &prometheus::BuildCounter()
                    .Name("psim_response_inputs_total")
                    .Help("Processed incident snapshots")
                    .Register(context().metrics().registry())
                    .Add({});
    missing_ = &prometheus::BuildCounter()
                    .Name("psim_response_plans_missing_total")
                    .Help("Incidents with no matching published plan")
                    .Register(context().metrics().registry())
                    .Add({});
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    const auto& config = context().config();
    const auto urls = config.get<std::vector<std::string>>("settings.handler_urls");
    std::set<std::string> handlers(urls.begin(), urls.end());
    for (const auto& url : handlers) {
      auto valid = p::security::validate_url(url);
      if (!valid) {
        co_return valid;
      }
    }
    repository_ = std::make_unique<Repository>(
        pool_.get(),
        [this](std::string topic, std::string key, const google::protobuf::Message& message) {
          return record(std::move(topic), std::move(key), message, {});
        },
        std::move(handlers),
        [ca = config.get<std::string>("settings.handler_ca_file")](std::string url, Json body, int timeout) {
          return invoke_handler(ca, std::move(url), std::move(body), timeout);
        });
    auto initialized = co_await repository_->initialize();
    if (!initialized) {
      co_return initialized;
    }
    api_ = std::make_unique<Api>(repository_.get());
    auto tls = co_await context().blocking().run([&config] {
      const auto directory = config.get<std::string>("settings.tls_directory");
      const auto certificate = config.get<std::string>("settings.tls_certificate_file");
      const auto key = config.get<std::string>("settings.tls_private_key_file");
      return p::security::tls_context(
          {.boundary = p::security::Boundary::kExternal,
           .ca_file = config.get<std::string>("settings.tls_ca_file"),
           .certificate_file = certificate.empty() ? directory + "/response-server.pem" : certificate,
           .private_key_file = key.empty() ? directory + "/response-server.key" : key,
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
    auto keys = co_await verifier_->refresh();
    if (!keys) {
      co_return keys;
    }
    relay_ = std::make_unique<p::pg::OutboxRelay>(pool_.get(), repository_->messages(),
                                                  p::pg::kafka_publisher(producer()), 512);
    launch(background(false));
    launch(background(true));
    launch(effects());
    acceptor_.open(Tcp::v4());
    acceptor_.set_option(Tcp::acceptor::reuse_address(true));
    acceptor_.bind({Tcp::v4(), static_cast<unsigned short>(config.get<int>("settings.listen_port"))});
    acceptor_.listen(64);
    healthy_.store(true);
    launch(accept());
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* /*output*/) override {
    const auto started = std::chrono::steady_clock::now();
    auto trace = p::stream::span(context(), "response.ingest", message->headers());
    auto input = decode<incident::v1::IncidentEventRecord>(*message);
    if (!input) {
      trace->End();
      co_return std::unexpected(input.error());
    }
    const auto missing = repository_->missing_plans();
    auto result = co_await repository_->incident(std::move(*input));
    if (result) {
      missing_->Increment(static_cast<double>(repository_->missing_plans() - missing));
    }
    latency_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    trace->End();
    if (!result) {
      co_return result;
    }
    ignored_->Increment();
    co_return p::Result<void>{};
  }

  void launch(asio::awaitable<void> operation) {
    ++background_;
    asio::co_spawn(executor(), std::move(operation), [this](const std::exception_ptr& error) {
      if (error) {
        healthy_.store(false);
        context().logger().error("response background operation failed");
      }
      --background_;
      done_.cancel();
    });
  }

  asio::awaitable<void> background(bool publishing) {
    auto& timer = publishing ? relay_timer_ : tick_;
    while (!stopping_) {
      p::Result<void> result;
      if (publishing) {
        auto emitted = co_await relay_->drain_once();
        if (!emitted) {
          result = std::unexpected(emitted.error());
        }
      } else {
        result = co_await repository_->tick();
      }
      (publishing ? relay_healthy_ : tick_healthy_).store(result.has_value());
      if (!result) {
        context().logger().warn("response background operation deferred",
                                {{"code", std::string(result.error().name())}});
      }
      auto interval = publishing ? 1 : 200;
      if (!result) {
        interval = 250;
      }
      timer.expires_after(std::chrono::milliseconds(interval));
      if (!stopping_) {
        co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
      }
    }
  }

  asio::awaitable<void> effects() {
    while (!stopping_) {
      auto result = co_await repository_->effects();
      if (!result) {
        context().logger().warn("response handler deferred", {{"code", std::string(result.error().name())}});
      }
      effect_timer_.expires_after(std::chrono::milliseconds(200));
      if (!stopping_) {
        co_await effect_timer_.async_wait(asio::as_tuple(asio::use_awaitable));
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
          auto result = co_await api_->handle(parser.release(), std::move(*principal));
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
          context().logger().warn("response HTTP session failed");
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
    healthy_.store(false);
    boost::system::error_code ignored;
    (void)acceptor_.close(ignored);
    tick_.cancel();
    relay_timer_.cancel();
    effect_timer_.cancel();
    for (const auto& socket : sessions_) {
      (void)beast::get_lowest_layer(*socket).socket().close(ignored);
    }
    while (background_ != 0 || !sessions_.empty()) {
      done_.expires_at(asio::steady_timer::time_point::max());
      co_await done_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    api_.reset();
    verifier_.reset();
    tls_.reset();
    relay_.reset();
    repository_.reset();
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
  }

 private:
  prometheus::Histogram* latency_ = nullptr;
  prometheus::Counter* ignored_ = nullptr;
  prometheus::Counter* missing_ = nullptr;
  Tcp::acceptor acceptor_;
  asio::steady_timer tick_, relay_timer_, effect_timer_, done_;
  std::atomic_bool healthy_{false}, tick_healthy_{true}, relay_healthy_{true};
  bool stopping_ = false;
  std::size_t background_ = 0;
  std::set<std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>> sessions_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<Repository> repository_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  std::unique_ptr<Api> api_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::shared_ptr<asio::ssl::context> tls_;
};

class ProjectionWorker final : public p::stream::Worker {
 public:
  ProjectionWorker(const p::runtime::Context& context, bool catalog)
      : p::stream::Worker(context, catalog ? "response-catalog" : "response-results",
                          catalog ? "psim.catalog.v1" : "psim.response.events.v1", p::kafka::Semantics::kAtLeastOnce),
        catalog_(catalog) {}

  std::string consumer_group() const override { return catalog_ ? "psim-response-catalog" : "psim-response-results"; }

  asio::awaitable<p::Result<void>> initialize() override {
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    repository_ = std::make_unique<Repository>(
        pool_.get(), [this](std::string topic, std::string key, const google::protobuf::Message& message) {
          return record(std::move(topic), std::move(key), message, {});
        });
    for (const auto* topic : {"psim.response.events.v1", "psim.audit.v1"}) {
      auto registered = co_await schema(topic);
      if (!registered) {
        co_return registered;
      }
    }
    co_return co_await repository_->initialize();
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* /*output*/) override {
    if (message->value().empty()) {
      co_return p::Result<void>{};
    }
    if (catalog_) {
      auto value = decode<catalog::v1::CatalogRecord>(*message);
      if (!value) {
        co_return std::unexpected(value.error());
      }
      co_return co_await repository_->catalog(std::move(*value));
    }
    auto value = decode<response::v1::ResponseEventRecord>(*message);
    if (!value) {
      co_return std::unexpected(value.error());
    }
    co_return co_await repository_->response(std::move(*value));
  }

  asio::awaitable<void> shutdown() override {
    repository_.reset();
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
    co_return;
  }

 private:
  bool catalog_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<Repository> repository_;
};
}  // namespace psim::response_engine

int main(int argc, char** argv) {
  return psim::platform::runtime::run(
      argc, argv,
      {.name = "response-engine",
       .settings_schema = psim::response_engine::settings(),
       .create = [](psim::platform::runtime::Context& context)
           -> psim::platform::Result<psim::platform::runtime::Components> {
         psim::platform::runtime::Components result;
         result.push_back(std::make_unique<psim::response_engine::Worker>(context));
         result.push_back(std::make_unique<psim::response_engine::ProjectionWorker>(context, true));
         result.push_back(std::make_unique<psim::response_engine::ProjectionWorker>(context, false));
         return result;
       }});
}
