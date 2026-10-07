#include "worker.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "psim/platform/config.hpp"
#include "psim/platform/log.hpp"
#include "psim/platform/metrics.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/tracing.hpp"
#include "settings.hpp"

namespace {

namespace asio = boost::asio;
using psim::platform::config::Config;
using psim::platform::config::Schema;
using psim::service_template::kSettingsSchema;
using psim::service_template::Worker;

Config configuration(std::map<std::string, std::string> settings) {
  const auto schema = Schema::parse(psim::platform::runtime::configuration_schema(kSettingsSchema)).value();
  return psim::platform::config::load(schema, {.file = std::nullopt, .environment = std::move(settings)}).value();
}

psim::platform::log::LoggerOptions logger_options() {
  psim::platform::log::LoggerOptions options;
  options.service = "service-template";
  options.instance = "t-0";
  return options;
}

psim::platform::observability::TracingOptions tracing_options() {
  psim::platform::observability::TracingOptions options;
  options.service = "service-template";
  options.sampling_ratio = 1.0;
  return options;
}

struct Fixture {
  asio::io_context io;
  std::shared_ptr<psim::platform::log::MemorySink> sink = std::make_shared<psim::platform::log::MemorySink>();
  psim::platform::log::Logger logger{logger_options(), sink};
  psim::platform::observability::Metrics metrics;
  psim::platform::observability::Tracing tracing{tracing_options()};
  Config config;
  std::unique_ptr<psim::platform::runtime::Context> context;

  explicit Fixture(Config c) : config(std::move(c)) {
    context = std::make_unique<psim::platform::runtime::Context>(io.get_executor(), logger, metrics, tracing, config);
  }
};

// Start a worker, let it run, drain it. The test owns what the pointers point to, so they outlive
// the coroutine (no captured references, cppcoreguidelines-avoid-capturing-lambda-coroutines).
asio::awaitable<void> run_then_drain(Worker* worker, std::chrono::milliseconds run_for, bool* drained) {
  EXPECT_TRUE((co_await worker->start()).has_value());
  EXPECT_TRUE(worker->ready());
  if (run_for.count() > 0) {
    asio::steady_timer run(co_await asio::this_coro::executor, run_for);
    co_await run.async_wait(asio::use_awaitable);
    EXPECT_GT(worker->stats().in_flight, 0U);  // messages are being processed when the stop comes
  }
  co_await worker->drain();
  *drained = true;
}

TEST(Worker, DrainFinishesEveryMessageInProgress) {
  Fixture f(configuration({{"PSIM__SETTINGS__INTERVAL_MS", "5"}, {"PSIM__SETTINGS__WORK_MS", "40"}}));
  Worker worker(*f.context);

  bool drained = false;
  asio::co_spawn(f.io, run_then_drain(&worker, std::chrono::milliseconds{100}, &drained), asio::detached);
  f.io.run();

  ASSERT_TRUE(drained);
  const auto stats = worker.stats();
  EXPECT_GT(stats.accepted, 5U);
  EXPECT_EQ(stats.processed, stats.accepted);
  EXPECT_EQ(stats.in_flight, 0U);
  EXPECT_FALSE(worker.ready());
  EXPECT_NE(f.metrics.expose().find("psim_template_messages_processed_total " + std::to_string(stats.processed)),
            std::string::npos);
}

TEST(Worker, DrainsImmediatelyWhenIdleAndAppliesANewInterval) {
  Fixture f(configuration({{"PSIM__SETTINGS__INTERVAL_MS", "1000"}}));
  Worker worker(*f.context);
  worker.reconfigure(configuration({{"PSIM__SETTINGS__INTERVAL_MS", "250"}}));

  bool drained = false;
  asio::co_spawn(f.io, run_then_drain(&worker, std::chrono::milliseconds{0}, &drained), asio::detached);
  f.io.run();

  EXPECT_EQ(worker.stats().accepted, 0U);
  const auto lines = f.sink->lines();
  EXPECT_NE(lines.at(0).find(R"("msg":"worker reconfigured","interval_ms":250)"), std::string::npos) << lines.at(0);
}

}  // namespace
