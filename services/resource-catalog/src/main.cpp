#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <psim/catalog/v1/catalog.pb.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "connector_admin.hpp"
#include "http.hpp"
#include "model.hpp"
#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/s3/client.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "repository.hpp"
#include "s3_files.hpp"

namespace psim::resource_catalog {
namespace p = platform;
namespace asio = boost::asio;
namespace beast = boost::beast;
using Tcp = asio::ip::tcp;

namespace {
std::string settings() {
  auto schema = Json::parse(p::stream::kSettings);
  auto& fields = schema["properties"];
  for (const auto& [name, value] :
       std::initializer_list<std::pair<std::string, std::string>>{{"tls_ca_file", ""},
                                                                  {"tls_certificate_file", ""},
                                                                  {"tls_private_key_file", ""},
                                                                  {"s3_endpoint", "https://s3:8333"},
                                                                  {"s3_bucket", "psim-plans"},
                                                                  {"s3_ca_file", ""},
                                                                  {"s3_access_key_file", ""},
                                                                  {"s3_secret_key_file", ""},
                                                                  {"spool_directory", "/tmp/psim-catalog"},
                                                                  {"management_endpoint", "connector-gateway:8080"},
                                                                  {"management_ca_file", ""},
                                                                  {"management_certificate_file", ""},
                                                                  {"management_key_file", ""},
                                                                  {"jwks_ca_file", ""}}) {
    fields[name] = {{"type", "string"}, {"default", value}};
  }
  return schema.dump();
}

}  // namespace

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "catalog", "psim.catalog.v1", p::kafka::Semantics::kAtLeastOnce, true),
        acceptor_(executor()),
        tick_(executor()),
        imports_(executor()),
        relay_timer_(executor()),
        cleanup_(executor()),
        done_(executor()) {}

  [[nodiscard]] bool ready() const override { return p::stream::Worker::ready() && healthy_.load(); }

  asio::awaitable<p::Result<void>> initialize() override {
    for (const auto* topic : {"psim.catalog.v1", "psim.config.v1", "psim.audit.v1"}) {
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
        pool_.get(), [this](std::string topic, std::string key, const google::protobuf::Message& message) {
          return record(std::move(topic), std::move(key), message, {});
        });
    auto initialized = co_await repository_->initialize();
    if (!initialized) {
      co_return initialized;
    }
    // Restore interval boundaries before accepting requests.
    auto restored = co_await repository_->tick(context().clock().wall());
    if (!restored) {
      co_return restored;
    }
    const auto& config = context().config();
    auto admin = co_await context().blocking().run([this, &config] {
      return ConnectorAdmin::create(&context().blocking(), config.get<std::string>("settings.management_endpoint"),
                                    config.get<std::string>("settings.management_ca_file"),
                                    config.get<std::string>("settings.management_certificate_file"),
                                    config.get<std::string>("settings.management_key_file"));
    });
    if (!admin) {
      co_return std::unexpected(admin.error());
    }
    admin_ = std::move(*admin);
    auto s3 = co_await p::s3::Client::create(
        &context().blocking(),
        {.endpoint = config.get<std::string>("settings.s3_endpoint"),
         .bucket = config.get<std::string>("settings.s3_bucket"),
         .access_key_file = config.get<std::string>("settings.s3_access_key_file"),
         .secret_key_file = config.get<std::string>("settings.s3_secret_key_file"),
         .tls = {.ca_file = config.get<std::string>("settings.s3_ca_file"), .require_client_certificate = false},
         .allow_plaintext = config.get<bool>("settings.allow_plaintext")});
    if (!s3) {
      co_return std::unexpected(s3.error());
    }
    s3_ = std::move(*s3);
    const auto directory = config.get<std::string>("settings.spool_directory");
    auto created = co_await context().blocking().run([&directory] {
      std::filesystem::create_directories(directory);
      std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
      // Transfer directories contain no authoritative data; discard abandoned transfers.
      for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_directory()
            && std::filesystem::file_time_type::clock::now() - entry.last_write_time() > std::chrono::hours(1)) {
          std::filesystem::remove_all(entry.path());
        }
      }
    });
    if (!created) {
      co_return std::unexpected(created.error());
    }
    files_ = std::make_unique<S3Files>(s3_.get(), &context().blocking(), directory);
    api_ = std::make_unique<Api>(repository_.get(), files_.get(), admin_.get(), &context().blocking());
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
    launch(background(0));
    launch(background(1));
    launch(background(2));
    launch(background(3));
    acceptor_.open(Tcp::v4());
    acceptor_.set_option(Tcp::acceptor::reuse_address(true));
    acceptor_.bind({Tcp::v4(), static_cast<unsigned short>(config.get<int>("settings.listen_port"))});
    acceptor_.listen(32);
    healthy_.store(true);
    auto trace = p::stream::span(context(), "catalog.initialize");
    trace->End();
    launch(accept());
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* message, p::kafka::Output* /*output*/) override {
    auto trace = p::stream::span(context(), "catalog.connector.project", message->headers());
    trace->End();
    auto event = decode<catalog::v1::CatalogRecord>(*message);
    if (!event) {
      co_return std::unexpected(event.error());
    }
    co_return co_await repository_->project(*event);
  }

  void launch(asio::awaitable<void> operation) {
    ++background_;
    asio::co_spawn(executor(), std::move(operation), [this](const std::exception_ptr& error) {
      if (error) {
        healthy_.store(false);
        context().logger().error("catalog background operation failed");
      }
      --background_;
      done_.cancel();
    });
  }

  asio::awaitable<void> background(std::size_t operation) {
    const std::array timers{&tick_, &imports_, &relay_timer_, &cleanup_};
    auto& timer = *timers.at(operation);
    while (!stopping_) {
      p::Result<void> result;
      if (operation == 0) {
        result = co_await repository_->tick(context().clock().wall());
      } else if (operation == 1) {
        result = co_await repository_->import_once();
      } else if (operation == 2) {
        auto published = co_await relay_->drain_once();
        if (!published) {
          result = std::unexpected(published.error());
        }
      } else {
        result =
            co_await repository_->cleanup_files([this](std::string key) { return files_->remove(std::move(key)); });
      }
      if (!result) {
        context().logger().warn("catalog background operation deferred",
                                {{"code", std::string(result.error().name())}});
      }
      auto interval = std::chrono::milliseconds(1);
      if (!result) {
        interval = std::chrono::milliseconds(250);
      } else if (operation == 0) {
        interval = std::chrono::milliseconds(100);
      } else if (operation == 3) {
        interval = std::chrono::milliseconds(10000);
      }
      timer.expires_after(interval);
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
    parser.body_limit(std::size_t{64} * 1024 * 1024);
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
      const auto request_path = std::string(parser.get().target());
      std::size_t limit = 65536;
      if (request_path.starts_with("/v1/imports")) {
        limit = std::size_t{64} * 1024 * 1024;
      } else if (request_path.find("/floor-plan") != std::string::npos) {
        limit = kMaxImageBytes;
      }
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
          auto result = co_await api_->handle(parser.release(), std::move(*principal), authorization.substr(7));
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
          context().logger().warn("catalog HTTP session failed");
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
    tick_.cancel();
    imports_.cancel();
    relay_timer_.cancel();
    cleanup_.cancel();
    for (const auto& socket : sessions_) {
      (void)beast::get_lowest_layer(*socket).socket().close(ignored);
    }
    while (background_ != 0 || !sessions_.empty()) {
      done_.expires_at(asio::steady_timer::time_point::max());
      co_await done_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    api_.reset();
    files_.reset();
    admin_.reset();
    verifier_.reset();
    tls_.reset();
    relay_.reset();
    repository_.reset();
    if (s3_) {
      s3_->close();
      s3_.reset();
    }
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
  }

 private:
  Tcp::acceptor acceptor_;
  asio::steady_timer tick_, imports_, relay_timer_, cleanup_, done_;
  std::atomic_bool healthy_{false};
  bool stopping_ = false;
  std::size_t background_ = 0;
  std::set<std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>> sessions_;
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<Repository> repository_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  std::unique_ptr<p::s3::Client> s3_;
  std::unique_ptr<S3Files> files_;
  std::unique_ptr<ConnectorAdmin> admin_;
  std::unique_ptr<Api> api_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::shared_ptr<asio::ssl::context> tls_;
};
}  // namespace psim::resource_catalog

int main(int argc, char** argv) {
  const psim::platform::connector::GrpcProcess grpc;
  const psim::platform::s3::Sdk s3;
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "resource-catalog",
                                       .settings_schema = psim::resource_catalog::settings(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::resource_catalog::Worker>(context));
                                         return result;
                                       }});
}
