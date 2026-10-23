#include "psim/platform/stream/runtime.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <google/protobuf/message.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <prometheus/counter.h>

#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/kafka/schema_registry.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/kafka/wire_format.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::platform::stream {

Result<security::JwksOptions> authentication_options(const runtime::Context& context) {
  const auto& config = context.config();
  auto secret = config.secret("settings.introspection_secret_file");
  if (!secret || secret->empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "token introspection secret required");
  }
  security::JwksOptions options{.jwt = {.issuer = config.get<std::string>("settings.issuer"), .audience = "psim"},
                                .url = config.get<std::string>("settings.jwks_url"),
                                .http = {.tls = {.ca_file = config.get<std::string>("settings.jwks_ca_file"),
                                                 .require_client_certificate = false},
                                         .allow_plaintext = config.get<bool>("settings.allow_plaintext")},
                                .introspection_url = config.get<std::string>("settings.introspection_url"),
                                .introspection_client = config.get<std::string>("settings.introspection_client"),
                                .introspection_secret = std::string(*secret)};
  if (options.introspection_url.empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "token introspection endpoint required");
  }
  return options;
}

Span span(const runtime::Context& context, std::string_view name, const kafka::Headers& headers) {
  opentelemetry::trace::StartSpanOptions options;
  options.parent = kafka::extract(headers);
  return context.tracer("psim.pipeline")->StartSpan(std::string(name), options);
}

void stamp(kafka::Headers& headers, std::string_view stage) {
  const auto now =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  headers.emplace_back("psim-ts-" + std::string(stage), std::to_string(now));
}

Worker::Worker(const runtime::Context& context, std::string stage, std::string input, kafka::Semantics semantics,
               bool broadcast, std::size_t shard)
    : context_(context),
      executor_(context.shards().executor(shard)),
      stage_(std::move(stage)),
      input_(std::move(input)),
      semantics_(semantics),
      broadcast_(broadcast),
      shard_(shard),
      stopped_(executor_) {}

asio::awaitable<Result<void>> Worker::schema(std::string topic) {
  const auto& config = context_.config();
  auto registry = kafka::SchemaRegistry::create(
      {.url = config.get<std::string>("settings.registry"),
       .http = {.tls = {.ca_file = config.get<std::string>("settings.registry_ca_file"),
                        .certificate_file = config.get<std::string>("settings.registry_certificate_file"),
                        .private_key_file = config.get<std::string>("settings.registry_private_key_file")},
                .allow_plaintext = config.get<bool>("settings.allow_plaintext")}});
  if (!registry) {
    co_return std::unexpected(registry.error());
  }
  auto id = co_await registry->latest_id(topic + "-value");
  if (!id) {
    co_return std::unexpected(id.error());
  }
  schemas_.emplace(std::move(topic), *id);
  co_return Result<void>{};
}

kafka::Record Worker::record(std::string topic, std::string key, const google::protobuf::Message& value,
                             kafka::Headers headers) const {
  std::string bytes;
  kafka::wire::write_header(bytes, schemas_.at(topic), kafka::wire::message_indexes(*value.GetDescriptor()));
  value.AppendToString(&bytes);
  stamp(headers, stage_);
  return {.topic = std::move(topic), .key = std::move(key), .value = std::move(bytes), .headers = std::move(headers)};
}

asio::awaitable<Result<std::unique_ptr<pg::Pool>>> Worker::database() {
  const auto& config = context_.config();
  const auto password = config.secret("settings.pg_password_file");
  if (!password) {
    co_return fail(ErrorCode::kCommonInvalidArgument, "database password file is required");
  }
  pg::PoolOptions options;
  options.size = 4;
  options.connection.host = config.get<std::string>("settings.pg_host");
  options.connection.port = config.get<std::string>("settings.pg_port");
  options.connection.database = config.get<std::string>("settings.pg_database");
  options.connection.user = config.get<std::string>("settings.pg_user");
  options.connection.max_result_bytes = config.get<std::size_t>("settings.pg_max_result_bytes");
  options.connection.password = std::string(*password);
  if (config.get<bool>("settings.allow_plaintext")) {
    options.connection.properties["sslmode"] = "disable";
  }
  for (const auto& [key, setting] :
       {std::pair{"sslrootcert", "settings.pg_ca_file"}, std::pair{"sslcert", "settings.pg_certificate_file"},
        std::pair{"sslkey", "settings.pg_private_key_file"}}) {
    const auto value = config.get<std::string>(setting);
    if (!value.empty()) {
      options.connection.properties[key] = value;
    }
  }
  co_return std::make_unique<pg::Pool>(std::move(options));
}

kafka::ClientOptions Worker::kafka_options(std::string client_id) const {
  const auto& config = context_.config();
  kafka::ClientOptions options{
      .bootstrap = config.get<std::string>("settings.brokers"),
      .client_id = std::move(client_id),
      .properties = {{"linger.ms", "1"}, {"fetch.wait.max.ms", "1"}, {"socket.nagle.disable", "true"}},
      .allow_plaintext = config.get<bool>("settings.allow_plaintext")};
  if (!options.allow_plaintext) {
    options.security = kafka::TransportSecurity{
        .tls = {.ca_file = config.get<std::string>("settings.kafka_ca_file"),
                .certificate_file = config.get<std::string>("settings.kafka_certificate_file"),
                .private_key_file = config.get<std::string>("settings.kafka_private_key_file")}};
  }
  return options;
}

asio::awaitable<Result<void>> Worker::open() {
  const auto& config = context_.config();
  const auto options = kafka_options(stage_ + "-" + config.get<std::string>("service.instance"));
  const auto transactional = semantics_ == kafka::Semantics::kExactlyOnce && !input_.empty();
  auto producer = kafka::Producer::create(
      executor_,
      {.client = options,
       .transactional_id = transactional ? options.client_id + "-" + std::to_string(shard_) : "",
       .delivery_timeout = std::chrono::milliseconds(10000),
       .transaction_timeout = transaction_timeout()},
      {.blocking = &context_.blocking(), .logger = &context_.logger()});
  if (!producer) {
    co_return std::unexpected(producer.error());
  }
  producer_ = std::move(*producer);
  auto initialized = co_await initialize();
  if (!initialized) {
    co_return std::unexpected(initialized.error());
  }
  if (!input_.empty()) {
    auto consumer = kafka::Consumer::create(
        executor_,
        {.client = options,
         .group_id =
             consumer_group().empty() ? options.client_id + (broadcast_ ? "-" + uuid7() : "") : consumer_group(),
         .topics = {input_}},
        {.blocking = &context_.blocking(), .logger = &context_.logger()});
    if (!consumer) {
      co_return std::unexpected(consumer.error());
    }
    consumer_ = std::move(*consumer);
    processor_ = std::make_unique<kafka::Processor>(
        consumer_.get(), producer_.get(), &context_.clock(),
        kafka::ProcessorOptions{.stage = stage_,
                                .semantics = semantics_,
                                .max_batch = batch_size(),
                                .commit_interval = commit_interval(),
                                .transaction_gap = transaction_gap(),
                                .timeout = std::chrono::milliseconds(10000)},
        kafka::Dependencies{.blocking = &context_.blocking(), .logger = &context_.logger()});
    running_ = true;
    asio::co_spawn(executor_, run(), asio::detached);
  }
  ready_.store(true);
  co_return Result<void>{};
}

asio::awaitable<void> Worker::run() {
  const auto result = co_await processor_->run(this);
  if (!result) {
    ready_.store(false);
    context_.logger().error("pipeline stopped", {{"code", std::string(result.error().name())}});
  }
  running_ = false;
  stopped_.cancel();
}

asio::awaitable<Result<void>> Worker::start() {
  auto result = co_await asio::co_spawn(executor_, open(), asio::use_awaitable);
  if (!result) {
    co_await asio::co_spawn(executor_, stop(), asio::use_awaitable);
  }
  co_return result;
}

asio::awaitable<void> Worker::stop() {
  ready_.store(false);
  if (processor_) {
    processor_->stop();
  }
  while (running_) {
    stopped_.expires_at(asio::steady_timer::time_point::max());
    co_await stopped_.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  if (consumer_) {
    co_await consumer_->close();
  }
  co_await shutdown();
  if (producer_) {
    (void)co_await producer_->flush(std::chrono::milliseconds(10000));
  }
  processor_.reset();
  consumer_.reset();
  producer_.reset();
}

asio::awaitable<void> Worker::drain() {
  co_await asio::co_spawn(executor_, stop(), asio::use_awaitable);
}
}  // namespace psim::platform::stream
