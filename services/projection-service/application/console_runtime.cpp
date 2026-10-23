#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <prometheus/histogram.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "console_replay.hpp"
#include "console_store.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/redis/pool.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "workers.hpp"

namespace psim::projection {
namespace asio = boost::asio;

class ConsoleWorker final : public p::stream::Worker {
 public:
  explicit ConsoleWorker(const p::runtime::Context& c)
      : p::stream::Worker(c, "console-models", ""), done_(executor()) {}

  bool ready() const override { return p::stream::Worker::ready() && healthy_.load(); }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* /*message*/, p::kafka::Output* /*output*/) override {
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> initialize() override {
    auto pool = co_await database();
    if (!pool) {
      co_return std::unexpected(pool.error());
    }
    pool_ = std::move(*pool);
    store_ = std::make_unique<ConsoleStore>(pool_.get(), "initial");
    auto initialized = co_await store_->initialize();
    if (!initialized) {
      co_return initialized;
    }
    auto lease = co_await pool_->acquire();
    if (!lease) {
      co_return std::unexpected(lease.error());
    }
    guard_.emplace(std::move(*lease));
    auto locked = co_await (*guard_)->query(
        {.sql = "SELECT pg_try_advisory_lock($1)", .parameters = {p::pg::Parameter::integer(kConsoleWriterLock)}});
    if (!locked || locked->value(0, 0) != "t") {
      co_return p::fail(p::ErrorCode::kCommonUnavailable, "console model already has a writer");
    }
    auto generation = co_await store_->active();
    if (!generation) {
      co_return std::unexpected(generation.error());
    }
    store_ = std::make_unique<ConsoleStore>(pool_.get(), *generation);
    store_->writer(&**guard_);
    auto source = p::kafka::Consumer::create(executor(),
                                             {.client = kafka_options("console-models"),
                                              .group_id = "console-models",
                                              .topics = console_topics(),
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
    auto prior = co_await store_->positions();
    if (!prior) {
      co_return std::unexpected(prior.error());
    }
    auto starts = bounds->begin;
    for (auto& [partition, offset] : starts) {
      if (prior->contains(partition)) {
        offset = std::max(offset, prior->at(partition));
      }
    }
    auto assigned = source_->assign(starts);
    if (!assigned) {
      co_return std::unexpected(assigned.error());
    }
    auto caught = co_await console_catch_up(
        store_.get(), source_.get(), bounds->end,
        std::chrono::milliseconds(context().config().get<int>("settings.console_replay_timeout_ms")));
    if (!caught) {
      co_return caught;
    }
    auto positioned = co_await source_->seek_and_wait(bounds->end, std::chrono::seconds(10));
    if (!positioned) {
      co_return positioned;
    }
    updates_ =
        &prometheus::BuildHistogram()
             .Name("psim_console_update_seconds")
             .Help("Kafka timestamp to committed live console update")
             .Register(context().metrics().registry())
             .Add({}, prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.2, 0.5, 1, 5});
    healthy_.store(true);
    running_ = true;
    asio::co_spawn(executor(), run(), [this](const std::exception_ptr& error) {
      if (error) {
        context().logger().error("console consumer failed");
      }
      healthy_.store(false);
      running_ = false;
      done_.cancel();
    });
    co_return p::Result<void>{};
  }

  asio::awaitable<void> run() {
    while (!stopping_) {
      const auto before = source_->positions();
      auto messages = co_await source_->poll(64, std::chrono::milliseconds(2));
      if (!messages) {
        context().logger().error("console Kafka read failed");
        co_return;
      }
      for (const auto& message : *messages) {
        auto trace = p::stream::span(context(), "console.project", message.headers());
        auto applied = co_await store_->apply(&message);
        trace->End();
        if (!applied) {
          context().logger().error("console write failed", {{"code", std::string(applied.error().name())}});
          co_return;
        }
        if (const auto timestamp = message.timestamp()) {
          updates_->Observe(
              std::max(0.0, std::chrono::duration<double>(std::chrono::system_clock::now() - *timestamp).count()));
        }
      }
      auto advanced =
          before == source_->positions() ? p::Result<void>{} : co_await store_->advance(source_->positions());
      if (!advanced) {
        context().logger().error("console cursor write failed");
        co_return;
      }
    }
  }

  asio::awaitable<void> shutdown() override {
    stopping_ = true;
    while (running_) {
      done_.expires_at(asio::steady_timer::time_point::max());
      co_await done_.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    if (source_) {
      co_await source_->close();
      source_.reset();
    }
    store_.reset();
    if (guard_) {
      (*guard_)->close();
      guard_.reset();
    }
    if (pool_) {
      pool_->close();
      pool_.reset();
    }
  }

 private:
  std::unique_ptr<p::pg::Pool> pool_;
  std::optional<p::pg::Pool::Lease> guard_;
  std::unique_ptr<ConsoleStore> store_;
  std::unique_ptr<p::kafka::Consumer> source_;
  asio::steady_timer done_;
  std::atomic_bool healthy_{false};
  bool stopping_ = false, running_ = false;
  prometheus::Histogram* updates_ = nullptr;
};

std::unique_ptr<p::runtime::Component> console_worker(const p::runtime::Context& context) {
  return std::make_unique<ConsoleWorker>(context);
}

std::unique_ptr<p::redis::Pool> console_redis(const p::runtime::Context& context) {
  const auto& config = context.config();
  if (config.get<std::string>("settings.redis_host").empty()) {
    return {};
  }
  p::redis::PoolOptions options;
  options.connection.endpoint.host = config.get<std::string>("settings.redis_host");
  options.connection.tls.enabled = !config.get<bool>("settings.allow_plaintext");
  options.connection.tls.ca_file = config.get<std::string>("settings.redis_ca_file");
  options.connection.tls.certificate_file = config.get<std::string>("settings.redis_certificate_file");
  options.connection.tls.private_key_file = config.get<std::string>("settings.redis_key_file");
  options.connection.connect_timeout = std::chrono::milliseconds(250);
  options.connection.command_timeout = std::chrono::milliseconds(250);
  options.size = 4;
  if (auto secret = config.secret("settings.redis_password_file")) {
    options.connection.credentials.password = std::string(*secret);
  }
  return std::make_unique<p::redis::Pool>(std::move(options));
}
}  // namespace psim::projection
