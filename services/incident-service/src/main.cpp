#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
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
#include <string_view>
#include <utility>

#include "directory.hpp"
#include "http.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/response/v1/events.pb.h"
#include "repository.hpp"

namespace psim::incident_service {
namespace beast = boost::beast;
using Tcp = asio::ip::tcp;

namespace {
std::string settings() {
  auto schema = Json::parse(p::stream::kSettings);
  auto& fields = schema["properties"];
  for (const auto* name :
       {"tls_ca_file", "tls_certificate_file", "tls_private_key_file", "jwks_ca_file", "directory_base",
        "directory_client", "directory_roles_client", "assistant_endpoint", "assistant_model"}) {
    fields[name] = {{"type", "string"}, {"default", ""}};
  }
  fields["directory_secret_file"] = {{"type", "string"}, {"x-psim-secret", true}};
  fields["legacy_types"] = {{"type", "boolean"}, {"default", false}};
  return schema.dump();
}
}  // namespace

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "incident", "psim.signals.v1", p::kafka::Semantics::kAtLeastOnce),
        acceptor_(executor()),
        tick_(executor()),
        relay_timer_(executor()),
        done_(executor()) {}

  std::string consumer_group() const override { return "psim-incident-service"; }

  std::size_t batch_size() const override { return 1; }

  [[nodiscard]] bool ready() const override { return p::stream::Worker::ready() && healthy_.load(); }

  asio::awaitable<p::Result<void>> initialize() override {
    for (const auto* topic : {"psim.incidents.events.v1", "psim.audit.v1", "psim.config.v1"}) {
      auto registered = co_await schema(topic);
      if (!registered) {
        co_return registered;
      }
    }
    latency_ =
        &prometheus::BuildHistogram()
             .Name("psim_incident_signal_seconds")
             .Help("Signal persistence transaction latency")
             .Register(context().metrics().registry())
             .Add({}, prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01, 0.02, 0.03, 0.04, 0.05, 0.1, 0.5, 1});
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    const auto& config = context().config();
    DirectoryOptions directory{.base = config.get<std::string>("settings.directory_base"),
                               .client = config.get<std::string>("settings.directory_client"),
                               .roles_client = config.get<std::string>("settings.directory_roles_client"),
                               .http = {.tls = {.ca_file = config.get<std::string>("settings.jwks_ca_file"),
                                                .require_client_certificate = false},
                                        .allow_plaintext = config.get<bool>("settings.allow_plaintext")}};
    if (auto secret = config.secret("settings.directory_secret_file")) {
      directory.secret = std::string(*secret);
    }
    directory_ = std::make_unique<Directory>(std::move(directory));
    repository_ = std::make_unique<Repository>(
        pool_.get(),
        [this](std::string topic, std::string key, const google::protobuf::Message& message) {
          return record(std::move(topic), std::move(key), message, {});
        },
        [this](std::string subject) { return directory_->lookup(std::move(subject)); },
        config.get<bool>("settings.legacy_types"),
        AssistantOptions{.endpoint = config.get<std::string>("settings.assistant_endpoint"),
                         .model = config.get<std::string>("settings.assistant_model"),
                         .http = {.tls = {.ca_file = config.get<std::string>("settings.jwks_ca_file"),
                                          .require_client_certificate = false},
                                  .timeout = std::chrono::milliseconds(5000),
                                  .max_body_bytes = 32768,
                                  .allow_plaintext = config.get<bool>("settings.allow_plaintext")}});
    auto initialized = co_await repository_->initialize();
    if (!initialized) {
      co_return initialized;
    }
    auto& operations = prometheus::BuildCounter()
                           .Name("psim_incident_operations_total")
                           .Help("Authorized operational requests by operation and outcome")
                           .Register(context().metrics().registry());
    auto& operations_latency = prometheus::BuildHistogram()
                                   .Name("psim_incident_operations_seconds")
                                   .Help("Operational request latency including provider transport")
                                   .Register(context().metrics().registry());
    api_ = std::make_unique<Api>(repository_.get(), &operations, &operations_latency);
    auto tls = co_await context().blocking().run([&config] {
      const auto directory = config.get<std::string>("settings.tls_directory");
      const auto certificate = config.get<std::string>("settings.tls_certificate_file");
      const auto key = config.get<std::string>("settings.tls_private_key_file");
      return p::security::tls_context(
          {.boundary = p::security::Boundary::kExternal,
           .ca_file = config.get<std::string>("settings.tls_ca_file"),
           .certificate_file = certificate.empty() ? directory + "/incident-server.pem" : certificate,
           .private_key_file = key.empty() ? directory + "/incident-server.key" : key,
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
    auto keys = co_await verifier_->refresh();
    if (!keys) {
      co_return keys;
    }
    relay_ = std::make_unique<p::pg::OutboxRelay>(pool_.get(), repository_->messages(),
                                                  p::pg::kafka_publisher(producer()), 512);
    launch(background(false));
    launch(background(true));
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
    auto trace = p::stream::span(context(), "incident.ingest", message->headers());
    auto signal = decode<processing::v1::SignalRecord>(*message);
    if (!signal || !signal->has_signal()) {
      trace->End();
      co_return p::fail(p::ErrorCode::kProcessingPayloadInvalid, "invalid signal");
    }
    auto headers = message->headers();
    p::kafka::inject(*trace, headers);
    auto result = co_await repository_->apply(
        std::move(*signal),
        [this, headers](std::string topic, std::string key, const google::protobuf::Message& value) {
          return record(std::move(topic), std::move(key), value, headers);
        });
    latency_->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    trace->End();
    co_return result;
  }

  void launch(asio::awaitable<void> operation) {
    ++background_;
    asio::co_spawn(executor(), std::move(operation), [this](const std::exception_ptr& error) {
      if (error) {
        healthy_.store(false);
        context().logger().error("incident background operation failed");
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
      if (!result) {
        context().logger().warn("incident background operation deferred",
                                {{"code", std::string(result.error().name())},
                                 {"error", std::string(result.error().message())},
                                 {"publishing", publishing}});
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
          auto request = parser.release();
          const bool assistant = request.target().find(":ask-assistant") != std::string_view::npos;
          auto result = co_await api_->handle(std::move(request), *principal);
          if (result && assistant) {
            auto current = co_await verifier_->verify(authorization.substr(7));
            if (!current || current->subject != principal->subject || current->tenant != principal->tenant
                || current->roles != principal->roles || current->scopes != principal->scopes) {
              auto logged = co_await repository_->denied(*principal, "assistant.delivery", {});
              result =
                  logged ? p::Result<Response>(p::fail(p::ErrorCode::kAuthForbidden, "assistant authorization changed"))
                         : p::Result<Response>(std::unexpected(logged.error()));
            }
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
          context().logger().warn("incident HTTP session failed");
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
    directory_.reset();
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
  }

 private:
  prometheus::Histogram* latency_ = nullptr;
  Tcp::acceptor acceptor_;
  asio::steady_timer tick_, relay_timer_, done_;
  std::atomic_bool healthy_{false};
  bool stopping_ = false;
  std::size_t background_ = 0;
  std::set<std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>> sessions_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<Repository> repository_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  std::unique_ptr<Directory> directory_;
  std::unique_ptr<Api> api_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::shared_ptr<asio::ssl::context> tls_;
};

class ResponseWorker final : public p::stream::Worker {
 public:
  explicit ResponseWorker(const p::runtime::Context& context)
      : p::stream::Worker(context, "incident-response", "psim.response.events.v1", p::kafka::Semantics::kAtLeastOnce) {}

  std::string consumer_group() const override { return "psim-incident-response"; }

  asio::awaitable<p::Result<void>> initialize() override {
    for (const auto* topic : {"psim.incidents.events.v1", "psim.audit.v1"}) {
      auto registered = co_await schema(topic);
      if (!registered) {
        co_return registered;
      }
    }
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    repository_ = std::make_unique<Repository>(
        pool_.get(), [this](std::string topic, std::string key, const google::protobuf::Message& value) {
          return record(std::move(topic), std::move(key), value, {});
        });
    co_return co_await repository_->initialize();
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* /*output*/) override {
    auto trace = p::stream::span(context(), "incident.response", message->headers());
    auto event = decode<response::v1::ResponseEventRecord>(*message);
    if (!event) {
      trace->End();
      co_return std::unexpected(event.error());
    }
    auto headers = message->headers();
    p::kafka::inject(*trace, headers);
    auto result = co_await repository_->apply_response(
        std::move(*event), [this, headers](std::string topic, std::string key, const google::protobuf::Message& value) {
          return record(std::move(topic), std::move(key), value, headers);
        });
    trace->End();
    co_return result;
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
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<Repository> repository_;
};
}  // namespace psim::incident_service

int main(int argc, char** argv) {
  return psim::platform::runtime::run(
      argc, argv,
      {.name = "incident-service",
       .settings_schema = psim::incident_service::settings(),
       .create = [](psim::platform::runtime::Context& context)
           -> psim::platform::Result<psim::platform::runtime::Components> {
         psim::platform::runtime::Components result;
         result.push_back(std::make_unique<psim::incident_service::Worker>(context));
         result.push_back(std::make_unique<psim::incident_service::ResponseWorker>(context));
         return result;
       }});
}
