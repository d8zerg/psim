#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "events.hpp"
#include "facts.hpp"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/clickhouse/loader.hpp"
#include "psim/platform/clickhouse/metrics.hpp"
#include "psim/platform/clickhouse/migrations.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/kafka/processor.hpp"
#include "psim/platform/kafka/producer.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/replica.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "workers.hpp"

namespace psim::event_history {
namespace asio = boost::asio;

namespace {
std::string settings() {
  auto schema = nlohmann::json::parse(p::stream::kSettings);
  auto& fields = schema["properties"];
  fields["pg_max_result_bytes"]["minimum"] = 16777216;
  fields["pg_max_result_bytes"]["default"] = 33554432;
  for (const auto& [name, value] :
       std::initializer_list<std::pair<std::string, std::string>>{{"clickhouse_host", "clickhouse"},
                                                                  {"clickhouse_cluster", "psim"},
                                                                  {"clickhouse_user", "psim"},
                                                                  {"clickhouse_password_file", ""},
                                                                  {"clickhouse_ca_file", ""},
                                                                  {"clickhouse_certificate_file", ""},
                                                                  {"clickhouse_private_key_file", ""}}) {
    fields[name] = {{"type", "string"}, {"default", value}};
  }
  for (const auto* name : {"tls_certificate_file", "tls_private_key_file", "tls_ca_file", "jwks_ca_file",
                           "history_read_user", "history_read_password_file"}) {
    fields[name] = {{"type", "string"}, {"default", ""}};
  }
  fields["history_batch_messages"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 50000}, {"default", 5000}};
  fields["history_batch_wait_ms"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 100}};
  fields["history_api"] = {{"type", "boolean"}, {"default", false}};
  fields["clickhouse_cluster"]["pattern"] = "^[A-Za-z0-9_]{1,64}$";
  fields["clickhouse_port"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}, {"default", 9440}};
  return schema.dump();
}
}  // namespace

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context, bool facts = false)
      : p::stream::Worker(context, facts ? "history-facts" : "event-history", ""),
        facts_mode_(facts),
        retry_(executor()),
        done_(executor()) {}

  bool ready() const override { return p::stream::Worker::ready() && healthy_.load(); }

  asio::awaitable<p::Result<void>> handle(const p::kafka::Message* /*message*/, p::kafka::Output* /*output*/) override {
    co_return p::Result<void>{};
  }

  asio::awaitable<p::Result<void>> initialize() override {
    auto journal = co_await database();
    if (!journal) {
      co_return std::unexpected(journal.error());
    }
    journal_ = std::move(*journal);
    auto initialized = co_await p::clickhouse::Loader::initialize(journal_.get());
    if (!initialized) {
      co_return initialized;
    }
    const auto& config = context().config();
    if (!facts_mode_) {
      metrics_ = std::make_unique<p::clickhouse::Metrics>(context().metrics().registry());
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
            .allow_plaintext = config.get<bool>("settings.allow_plaintext")},
        metrics_.get());
    const auto cluster = config.get<std::string>("settings.clickhouse_cluster");
    p::clickhouse::Migrator migrator(destination_.get(), journal_.get(), "event_history_" + cluster);
    auto migrated = co_await migrator.apply({migration(cluster), facts_migration(cluster)});
    if (!migrated) {
      co_return migrated;
    }
    const p::kafka::Dependencies deps{.blocking = &context().blocking(), .logger = &context().logger()};
    auto control =
        co_await p::stream::Replica::open(executor(), kafka_options("history-control"), deps, {"psim.config.v1"},
                                          [this](const p::kafka::Message& message) -> p::Result<void> {
                                            auto record = decode<config::v1::ConfigRecord>(message);
                                            if (!record) {
                                              return std::unexpected(record.error());
                                            }
                                            return facts_mode_ ? facts_.config(*record) : events_.config(*record);
                                          });
    if (!control) {
      co_return std::unexpected(control.error());
    }
    control_ = std::move(*control);
    auto options = kafka_options(std::string(facts_mode_ ? "history-facts-" : "event-history-")
                                 + config.get<std::string>("service.instance"));
    auto consumer = p::kafka::Consumer::create(
        executor(),
        {.client = options,
         .group_id = facts_mode_ ? "psim-history-facts" : "psim-event-history",
         .topics = facts_mode_ ? std::vector<std::string>{"psim.signals.v1", "psim.incidents.events.v1"}
                               : std::vector<std::string>{"psim.events.normalized.v1"}},
        deps);
    if (!consumer) {
      co_return std::unexpected(consumer.error());
    }
    consumer_ = std::move(*consumer);
    auto producer =
        p::kafka::Producer::create(executor(), {.client = options, .transactional_id = options.client_id}, deps);
    if (!producer) {
      co_return std::unexpected(producer.error());
    }
    output_ = std::move(*producer);
    auto transactions = co_await output_->init_transactions(std::chrono::seconds(30));
    if (!transactions) {
      co_return transactions;
    }
    loader_ = std::make_unique<p::clickhouse::Loader>(
        destination_.get(), journal_.get(), consumer_.get(), output_.get(),
        p::clickhouse::LoaderOptions{
            .name = facts_mode_ ? "history_facts_v1" : "events_v1",
            .table = facts_mode_ ? "history_facts" : "events",
            .dlq = "psim.dlq.event-history.v1",
            .max_messages = config.get<std::size_t>("settings.history_batch_messages"),
            .max_bytes = std::min<std::size_t>(std::size_t{8} * 1024 * 1024,
                                               (config.get<std::size_t>("settings.pg_max_result_bytes") - 65536) / 2),
            .flush_after = std::chrono::milliseconds(config.get<int>("settings.history_batch_wait_ms"))},
        [this](auto messages) { return facts_mode_ ? facts_.convert(messages) : events_.convert(messages); },
        metrics_.get());
    running_ = true;
    healthy_.store(true);
    asio::co_spawn(executor(), run(), [this](const std::exception_ptr& error) {
      if (error) {
        context().logger().error("event history background operation failed");
      }
      healthy_.store(false);
      running_ = false;
      done_.cancel();
    });
    co_return p::Result<void>{};
  }

  asio::awaitable<void> run() {
    while (!stopping_) {
      auto updated = co_await control_->update();
      if (!updated) {
        context().logger().error("event history configuration failed");
        co_return;
      }
      auto result = co_await loader_->run_once();
      if (!result) {
        healthy_.store(false);
        context().logger().warn("event history batch deferred", {{"code", std::string(result.error().name())}});
        if (output_->fatal()) {
          co_return;
        }
        retry_.expires_after(std::chrono::milliseconds(250));
        if (!stopping_) {
          co_await retry_.async_wait(asio::as_tuple(asio::use_awaitable));
        }
      } else {
        healthy_.store(true);
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
    loader_.reset();
    if (consumer_) {
      co_await consumer_->close();
      consumer_.reset();
    }
    output_.reset();
    if (control_) {
      co_await control_->close();
      control_.reset();
    }
    if (destination_) {
      destination_->close();
      destination_.reset();
    }
    if (journal_) {
      journal_->close();
      journal_.reset();
    }
    metrics_.reset();
  }

 private:
  bool facts_mode_ = false;
  Facts facts_;
  Events events_;
  asio::steady_timer retry_, done_;
  std::atomic_bool healthy_{false};
  bool stopping_ = false, running_ = false;
  std::unique_ptr<p::pg::Pool> journal_;
  std::unique_ptr<p::clickhouse::Metrics> metrics_;
  std::unique_ptr<p::clickhouse::Pool> destination_;
  std::unique_ptr<p::stream::Replica> control_;
  std::unique_ptr<p::kafka::Consumer> consumer_;
  std::unique_ptr<p::kafka::Producer> output_;
  std::unique_ptr<p::clickhouse::Loader> loader_;
};
}  // namespace psim::event_history

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "event-history",
                                       .settings_schema = psim::event_history::settings(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::event_history::Worker>(context));
                                         result.push_back(std::make_unique<psim::event_history::Worker>(context, true));
                                         result.push_back(psim::event_history::history_retention(context));
                                         if (context.config().get<bool>("settings.history_api")) {
                                           result.push_back(psim::event_history::history_http(context));
                                         }
                                         return result;
                                       }});
}
