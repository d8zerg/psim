#include "management.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "history.hpp"
#include "http.hpp"
#include "psim/api/v1/rules.pb.h"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/store.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/platform/security/tls.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "repository.hpp"
#include "rules.hpp"

namespace psim::correlation::management {
namespace beast = boost::beast;
using Tcp = asio::ip::tcp;
using Time = google::protobuf::util::TimeUtil;

std::string settings() {
  auto schema = Json::parse(p::stream::kSettings);
  auto& fields = schema["properties"];
  fields["role"] = {{"type", "string"}, {"enum", {"live", "management"}}, {"default", "live"}};
  fields["pg_max_result_bytes"]["default"] = 33554432;
  for (const auto& [name, value] : std::initializer_list<std::pair<std::string, std::string>>{
           {"taxonomy_file", "/usr/share/psim/correlation-taxonomy.yaml"},
           {"tls_ca_file", ""},
           {"tls_certificate_file", ""},
           {"tls_private_key_file", ""},
           {"jwks_ca_file", ""},
           {"replay_brokers", ""},
           {"replay_kafka_certificate_file", ""},
           {"replay_kafka_private_key_file", ""},
           {"clickhouse_host", "clickhouse"},
           {"clickhouse_user", "rule-replay"},
           {"clickhouse_password_file", ""},
           {"clickhouse_ca_file", ""},
           {"clickhouse_certificate_file", ""},
           {"clickhouse_private_key_file", ""}}) {
    fields[name] = {{"type", "string"}, {"default", value}};
  }
  fields["clickhouse_password_file"].erase("default");
  fields["clickhouse_port"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}, {"default", 9440}};
  fields["clickhouse_password_file"]["x-psim-secret"] = true;
  return schema.dump();
}

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "rule-management", ""),
        acceptor_(executor()),
        jobs_(executor()),
        relay_timer_(executor()),
        done_(executor()) {}

  bool ready() const override { return p::stream::Worker::ready() && healthy_.load(); }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* /*message*/, p::kafka::Output* /*output*/) override {
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> initialize() override {
    for (const auto* topic : {"psim.config.v1", "psim.audit.v1"}) {
      auto registered = co_await schema(topic);
      if (!registered) {
        co_return registered;
      }
    }
    const auto& config = context().config();
    const auto path = config.get<std::string>("settings.taxonomy_file");
    auto taxonomy = co_await context().blocking().run([path] { return rules::load_types(path); });
    if (!taxonomy) {
      co_return std::unexpected(taxonomy.error());
    }
    taxonomy_ = std::move(*taxonomy);
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    repository_ = std::make_unique<Repository>(
        pool_.get(),
        [this](std::string topic, std::string key, const google::protobuf::Message& value) {
          return record(std::move(topic), std::move(key), value, {});
        },
        taxonomy_);
    auto initialized = co_await repository_->initialize();
    if (!initialized) {
      co_return initialized;
    }
    destination_ = std::make_unique<p::clickhouse::Pool>(
        &context().blocking(),
        p::clickhouse::PoolOptions{
            .endpoints = {{config.get<std::string>("settings.clickhouse_host"),
                           config.get<std::uint16_t>("settings.clickhouse_port")}},
            .user = config.get<std::string>("settings.clickhouse_user"),
            .password_file = config.get<std::string>("settings.clickhouse_password_file"),
            .tls = {.ca_file = config.get<std::string>("settings.clickhouse_ca_file"),
                    .certificate_file = config.get<std::string>("settings.clickhouse_certificate_file"),
                    .private_key_file = config.get<std::string>("settings.clickhouse_private_key_file")},
            .allow_plaintext = config.get<bool>("settings.allow_plaintext"),
            .size = 1,
            .query_timeout = std::chrono::seconds(15),
            .max_result_rows = 100001});
    auto options = kafka_options("rule-replay");
    const auto replay_brokers = config.get<std::string>("settings.replay_brokers");
    if (!replay_brokers.empty()) {
      options.bootstrap = replay_brokers;
    }
    const auto replay_certificate = config.get<std::string>("settings.replay_kafka_certificate_file");
    if (options.security || !replay_certificate.empty()) {
      options.allow_plaintext = false;
      options.security = p::kafka::TransportSecurity{
          .tls = {.ca_file = config.get<std::string>("settings.kafka_ca_file"),
                  .certificate_file = replay_certificate,
                  .private_key_file = config.get<std::string>("settings.replay_kafka_private_key_file")}};
      if (replay_certificate.empty() || options.security->tls.private_key_file.empty()
          || replay_certificate == config.get<std::string>("settings.kafka_certificate_file")) {
        co_return p::fail(p::ErrorCode::kCommonInvalidArgument, "a separate replay Kafka identity is required");
      }
    }
    history_ = std::make_unique<History>(
        executor(), std::move(options),
        p::kafka::Dependencies{.blocking = &context().blocking(), .logger = &context().logger()}, destination_.get());
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
    auto refreshed = co_await verifier_->refresh();
    if (!refreshed) {
      co_return refreshed;
    }
    api_ = std::make_unique<Api>(repository_.get());
    relay_ = std::make_unique<p::pg::OutboxRelay>(pool_.get(), repository_->messages(),
                                                  p::pg::kafka_publisher(producer()), 32);
    acceptor_.open(Tcp::v4());
    acceptor_.set_option(Tcp::acceptor::reuse_address(true));
    acceptor_.bind({Tcp::v4(), config.get<unsigned short>("settings.listen_port")});
    acceptor_.listen(32);
    healthy_.store(true);
    launch(background(true));
    launch(background(false));
    launch(accept());
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> process() {
    auto claimed = co_await repository_->take_job();
    if (!claimed) {
      co_return std::unexpected(claimed.error());
    }
    if (!*claimed) {
      co_return p::Result<void>{};
    }
    auto job = std::move(**claimed);
    running_job_ = Job{.tenant = job.tenant,
                       .id = job.id,
                       .lease = job.lease,
                       .principal = {},
                       .request = {},
                       .rule = {},
                       .input = {}};
    auto result = co_await execute(std::move(job));
    running_job_.reset();
    co_return result;
  }

  asio::awaitable<p::Result<void>> execute(Job job) {
    auto input = job.input.is_null() ? co_await history_->read(job) : restore_input(job.input);
    if (!input) {
      context().logger().warn(
          "historical replay input failed",
          {{"job", job.id}, {"code", std::string(input.error().name())}, {"reason", input.error().message()}});
      if (stopping_) {
        co_return std::unexpected(input.error());
      }
      co_return co_await repository_->finish(std::move(job), std::unexpected(input.error()));
    }
    if (job.input.is_null()) {
      auto frozen = co_await context().blocking().run([&input] { return snapshot(*input); });
      if (!frozen) {
        co_return co_await repository_->finish(std::move(job), std::unexpected(frozen.error()));
      }
      auto saved = co_await repository_->save_input(job, std::move(*frozen));
      if (!saved) {
        co_return co_await repository_->finish(std::move(job), std::unexpected(saved.error()));
      }
    }
    auto result = co_await context().blocking().run([this, &job, &input]() -> p::Result<rules::ReplayResult> {
      rules::Rule rule;
      api::v1::StartDryRunRequest request;
      if (!google::protobuf::util::JsonStringToMessage(job.rule.dump(), &rule).ok()
          || !google::protobuf::util::JsonStringToMessage(job.request.dump(), &request).ok()) {
        return p::fail(p::ErrorCode::kProcessingRuleInvalid, "invalid stored dry-run rule");
      }
      auto compiled = rules::compile(rule, taxonomy_);
      if (!compiled.compiled) {
        return p::fail(p::ErrorCode::kProcessingRuleInvalid, "stored rule no longer validates");
      }
      return rules::replay(*compiled.compiled, std::move(input->events), input->context, job.tenant,
                           Time::TimestampToMilliseconds(request.from()), Time::TimestampToMilliseconds(request.to()));
    });
    co_return co_await repository_->finish(std::move(job), std::move(result));
  }

  void launch(asio::awaitable<void> operation) {
    ++background_;
    asio::co_spawn(executor(), std::move(operation), [this](const std::exception_ptr& error) {
      if (error) {
        running_job_.reset();
        healthy_.store(false);
        context().logger().error("rule management operation failed");
      }
      --background_;
      done_.cancel();
    });
  }

  asio::awaitable<void> background(bool jobs) {
    auto& timer = jobs ? jobs_ : relay_timer_;
    while (!stopping_) {
      p::Result<void> result;
      if (jobs) {
        result = co_await process();
      } else {
        if (running_job_ && std::chrono::steady_clock::now() >= heartbeat_) {
          const auto token = *running_job_;
          auto renewed = co_await repository_->renew(token);
          if (!renewed) {
            context().logger().warn("dry-run lease renewal deferred");
          }
          heartbeat_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        }
        auto sent = co_await relay_->drain_once();
        if (!sent) {
          result = std::unexpected(sent.error());
        }
      }
      if (!result) {
        context().logger().warn("rule management operation deferred", {{"code", std::string(result.error().name())}});
      }
      timer.expires_after(std::chrono::milliseconds(result ? 100 : 500));
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
    parser.body_limit(65536);
    auto [header, bytes] =
        co_await http::async_read_header(*socket, buffer, parser, asio::as_tuple(asio::use_awaitable));
    (void)bytes;
    if (header) {
      co_return;
    }
    p::Result<p::security::Principal> principal = p::fail(p::ErrorCode::kAuthUnauthenticated, "Bearer token required");
    const std::string authorization(parser.get()[http::field::authorization]);
    if (authorization.starts_with("Bearer ") && authorization.size() <= 8192) {
      principal = co_await verifier_->verify(authorization.substr(7));
    }
    Response response;
    if (!principal) {
      response = Api::problem(principal.error());
    } else {
      beast::get_lowest_layer(*socket).expires_after(std::chrono::seconds(30));
      auto [error, count] = co_await http::async_read(*socket, buffer, parser, asio::as_tuple(asio::use_awaitable));
      (void)count;
      if (error) {
        response = Api::problem(p::Error(p::ErrorCode::kCommonInvalidArgument, "invalid or excessive request body"));
      } else {
        auto result = co_await api_->handle(parser.release(), std::move(*principal));
        response = result ? std::move(*result) : Api::problem(result.error());
      }
    }
    response.keep_alive(false);
    response.set(http::field::cache_control, "no-store");
    response.set(http::field::x_content_type_options, "nosniff");
    response.prepare_payload();
    beast::get_lowest_layer(*socket).expires_after(std::chrono::seconds(10));
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
          context().logger().warn("rule HTTP session failed");
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
    jobs_.cancel();
    relay_timer_.cancel();
    if (history_) {
      history_->stop();
    }
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
    history_.reset();
    repository_.reset();
    if (destination_) {
      destination_->close();
      destination_.reset();
    }
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
  }

 private:
  Tcp::acceptor acceptor_;
  asio::steady_timer jobs_, relay_timer_, done_;
  std::atomic_bool healthy_{false};
  bool stopping_ = false;
  std::size_t background_ = 0;
  std::set<std::shared_ptr<beast::ssl_stream<beast::tcp_stream>>> sessions_{};
  std::set<std::string> taxonomy_{};
  std::optional<Job> running_job_{};
  std::chrono::steady_clock::time_point heartbeat_{};
  std::unique_ptr<p::pg::Pool> pool_;
  std::unique_ptr<Repository> repository_;
  std::unique_ptr<p::pg::OutboxRelay> relay_;
  std::unique_ptr<p::clickhouse::Pool> destination_;
  std::unique_ptr<History> history_;
  std::unique_ptr<Api> api_;
  std::unique_ptr<p::security::JwksCache> verifier_;
  std::shared_ptr<asio::ssl::context> tls_;
};

p::Result<std::unique_ptr<p::runtime::Component>> create(const p::runtime::Context& context) {
  if (context.config().get<std::size_t>("runtime.shards") != 1) {
    return p::fail(p::ErrorCode::kCommonInvalidArgument, "rule management requires one shard per process");
  }
  return std::make_unique<Worker>(context);
}
}  // namespace psim::correlation::management
