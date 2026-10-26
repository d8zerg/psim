#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "facts.hpp"
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
#include "psim/platform/stream/runtime.hpp"
#include "workers.hpp"

namespace psim::projection {
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
                           "reports_read_user", "reports_read_password_file"}) {
    fields[name] = {{"type", "string"}, {"default", ""}};
  }
  fields["reports_batch_messages"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 50000}, {"default", 5000}};
  fields["reports_batch_wait_ms"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 100}};
  fields["clickhouse_cluster"]["pattern"] = "^[A-Za-z0-9_]{1,64}$";
  fields["clickhouse_port"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 65535}, {"default", 9440}};
  for (const auto* name : {"reports_enabled", "console_enabled", "console_writer"}) {
    fields[name] = {{"type", "boolean"}, {"default", true}};
  }
  fields["console_replay_timeout_ms"] = {
      {"type", "integer"}, {"minimum", 1000}, {"maximum", 3600000}, {"default", 300000}};
  for (const auto* name : {"redis_host", "redis_ca_file", "redis_certificate_file", "redis_key_file"}) {
    fields[name] = {{"type", "string"}, {"default", ""}};
  }
  fields["redis_password_file"] = {{"type", "string"}, {"x-psim-secret", true}};
  return schema.dump();
}
}  // namespace

class Worker final : public p::stream::Worker {
 public:
  explicit Worker(const p::runtime::Context& context)
      : p::stream::Worker(context, "incident-facts", ""), retry_(executor()), done_(executor()) {}

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
    {
      metrics_ = std::make_unique<p::clickhouse::Metrics>(context().metrics().registry());
    }
    destination_ = connection(context());
    const auto cluster = config.get<std::string>("settings.clickhouse_cluster");
    p::clickhouse::Migrator migrator(destination_.get(), journal_.get(), "incident_reports_" + cluster);
    auto migrated = co_await migrator.apply({migration(cluster)});
    if (!migrated) {
      co_return migrated;
    }
    const p::kafka::Dependencies deps{.blocking = &context().blocking(), .logger = &context().logger()};
    auto options = kafka_options(std::string("incident-facts-") + config.get<std::string>("service.instance"));
    auto consumer = p::kafka::Consumer::create(
        executor(),
        {.client = options,
         .group_id = "psim-projection-service",
         .topics = std::vector<std::string>{"psim.incidents.events.v1", "psim.response.events.v1"}},
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
            .name = "incident_facts_v1",
            .table = "incident_facts",
            .dlq = "psim.dlq.projection.v1",
            .max_messages = config.get<std::size_t>("settings.reports_batch_messages"),
            .max_bytes = std::min<std::size_t>(std::size_t{8} * 1024 * 1024,
                                               (config.get<std::size_t>("settings.pg_max_result_bytes") - 65536) / 2),
            .flush_after = std::chrono::milliseconds(config.get<int>("settings.reports_batch_wait_ms"))},
        [](auto messages) { return convert(messages); }, metrics_.get());
    running_ = true;
    healthy_.store(true);
    asio::co_spawn(executor(), run(), [this](const std::exception_ptr& error) {
      if (error) {
        context().logger().error("incident facts background operation failed");
      }
      healthy_.store(false);
      running_ = false;
      done_.cancel();
      if (!stopping_) {
        context().request_failure();
      }
    });
    co_return p::Result<void>{};
  }

  asio::awaitable<void> run() {
    while (!stopping_) {
      auto result = co_await loader_->run_once();
      if (!result) {
        healthy_.store(false);
        context().logger().warn("incident facts batch deferred", {{"code", std::string(result.error().name())},
                                                                  {"error", std::string(result.error().message())}});
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
  asio::steady_timer retry_, done_;
  std::atomic_bool healthy_{false};
  bool stopping_ = false, running_ = false;
  std::unique_ptr<p::pg::Pool> journal_;
  std::unique_ptr<p::clickhouse::Metrics> metrics_;
  std::unique_ptr<p::clickhouse::Pool> destination_;
  std::unique_ptr<p::kafka::Consumer> consumer_;
  std::unique_ptr<p::kafka::Producer> output_;
  std::unique_ptr<p::clickhouse::Loader> loader_;
};
}  // namespace psim::projection

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "projection-service",
                                       .settings_schema = psim::projection::settings(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         if (context.config().get<bool>("settings.reports_enabled")) {
                                           result.push_back(std::make_unique<psim::projection::Worker>(context));
                                         }
                                         if (context.config().get<bool>("settings.console_enabled")
                                             && context.config().get<bool>("settings.console_writer")) {
                                           result.push_back(psim::projection::console_worker(context));
                                         }
                                         result.push_back(psim::projection::reports_http(context));
                                         return result;
                                       }});
}
