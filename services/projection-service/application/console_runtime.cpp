#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/span.h>
#include <prometheus/gauge.h>
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
#include <vector>

#include "console_replay.hpp"
#include "console_store.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/redis/pool.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/redis.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "workers.hpp"

namespace psim::projection {
namespace asio = boost::asio;

class ConsoleWorker final : public p::stream::Worker {
 public:
  explicit ConsoleWorker(const p::runtime::Context& c)
      : p::stream::Worker(c, "console-models", ""), done_(executor()), retry_(executor()) {}

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
    updates_ =
        &prometheus::BuildHistogram()
             .Name("psim_console_update_seconds")
             .Help("Kafka timestamp to committed live console update")
             .Register(context().metrics().registry())
             .Add({}, prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.2, 0.5, 1, 5});
    writer_ = &prometheus::BuildGauge()
                   .Name("psim_console_writer_active")
                   .Help("Whether this instance owns the console writer session")
                   .Register(context().metrics().registry())
                   .Add({});
    running_ = true;
    asio::co_spawn(executor(), supervise(), [this](const std::exception_ptr& error) {
      if (error) {
        context().logger().error("console consumer failed");
      }
      // Close the session even if an unexpected exception interrupts supervision.
      if (guard_) {
        (*guard_)->close();
      }
      writer_->Set(0);
      healthy_.store(false);
      running_ = false;
      done_.cancel();
    });
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<bool>> start_writer() {
    auto lease = co_await pool_->acquire();
    if (!lease) {
      co_return std::unexpected(lease.error());
    }
    guard_.emplace(std::move(*lease));
    auto locked = co_await (*guard_)->query(
        {.sql = "SELECT pg_try_advisory_lock($1)", .parameters = {p::pg::Parameter::integer(kConsoleWriterLock)}});
    if (!locked) {
      co_return std::unexpected(locked.error());
    }
    if (locked->value(0, 0) != "t") {
      guard_.reset();
      co_return false;
    }
    // Fence a suspended writer independently of TCP keepalives. All writes use
    // this session, so a resumed process cannot write after its lock expires.
    for (const auto* sql : {"SET idle_session_timeout = '15s'", "SET idle_in_transaction_session_timeout = '15s'",
                            "SET statement_timeout = '10s'"}) {
      auto configured = co_await (*guard_)->query({.sql = sql});
      if (!configured) {
        co_return std::unexpected(configured.error());
      }
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
      co_return std::unexpected(caught.error());
    }
    auto positioned = co_await source_->seek_and_wait(bounds->end, std::chrono::seconds(10));
    if (!positioned) {
      co_return std::unexpected(positioned.error());
    }
    co_return true;
  }

  asio::awaitable<void> release_writer() {
    writer_->Set(0);
    store_->writer(nullptr);
    if (guard_) {
      (*guard_)->close();
      guard_.reset();
    }
    if (source_) {
      co_await source_->close();
      source_.reset();
    }
  }

  asio::awaitable<void> supervise() {
    while (!stopping_) {
      auto started = co_await start_writer();
      if (started) {
        healthy_.store(true);
        if (*started) {
          writer_->Set(1);
          context().logger().info("console writer acquired");
          co_await run();
          healthy_.store(false);
        }
      } else {
        healthy_.store(false);
        context().logger().warn("console writer initialization deferred",
                                {{"code", std::string(started.error().name())}});
      }
      co_await release_writer();
      if (!stopping_) {
        retry_.expires_after(std::chrono::seconds(1));
        co_await retry_.async_wait(asio::as_tuple(asio::use_awaitable));
      }
    }
  }

  asio::awaitable<void> run() {
    if (!guard_) {
      co_return;
    }
    auto heartbeat = std::chrono::steady_clock::now();
    while (!stopping_) {
      if (std::chrono::steady_clock::now() >= heartbeat) {
        auto alive = co_await (*guard_)->query({.sql = "SELECT 1"});
        if (!alive) {
          context().logger().warn("console writer session lost");
          co_return;
        }
        heartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      }
      auto written = source_->positions();
      auto messages = co_await source_->poll(1024, std::chrono::milliseconds(2));
      if (!messages) {
        context().logger().error("console Kafka read failed");
        co_return;
      }
      std::vector<opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>> traces;
      for (const auto& message : *messages) {
        traces.push_back(p::stream::span(context(), "console.project", message.headers()));
      }
      auto applied = co_await store_->apply_batch(*messages);
      for (const auto& trace : traces) {
        trace->End();
      }
      if (!applied) {
        context().logger().error("console write failed", {{"code", std::string(applied.error().name())}});
        co_return;
      }
      for (const auto& message : *messages) {
        auto& offset = written[message.topic_partition()];
        offset = std::max(offset, message.offset() + 1);
        if (const auto timestamp = message.timestamp()) {
          updates_->Observe(
              std::max(0.0, std::chrono::duration<double>(std::chrono::system_clock::now() - *timestamp).count()));
        }
      }
      p::kafka::Offsets changed;
      for (const auto& [partition, offset] : source_->positions()) {
        const auto previous = written.find(partition);
        if (previous == written.end() || previous->second < offset) {
          changed.emplace(partition, offset);
        }
      }
      auto advanced = changed.empty() ? p::Result<void>{} : co_await store_->advance(std::move(changed));
      if (!advanced) {
        context().logger().error("console cursor write failed");
        co_return;
      }
    }
  }

  asio::awaitable<void> shutdown() override {
    stopping_ = true;
    retry_.cancel();
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
  asio::steady_timer retry_;
  std::atomic_bool healthy_{false};
  bool stopping_ = false, running_ = false;
  prometheus::Histogram* updates_ = nullptr;
  prometheus::Gauge* writer_ = nullptr;
};

std::unique_ptr<p::runtime::Component> console_worker(const p::runtime::Context& context) {
  return std::make_unique<ConsoleWorker>(context);
}

std::unique_ptr<p::redis::Pool> console_redis(const p::runtime::Context& context) {
  const auto& config = context.config();
  if (config.get<std::string>("settings.redis_host").empty()) {
    return {};
  }
  auto options = p::stream::redis_options(config);
  options.connection.connect_timeout = std::chrono::milliseconds(250);
  options.connection.command_timeout = std::chrono::milliseconds(250);
  options.size = 4;
  return std::make_unique<p::redis::Pool>(std::move(options));
}
}  // namespace psim::projection
