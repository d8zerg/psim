#include "worker.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <opentelemetry/trace/scope.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>

#include <chrono>
#include <cstdint>

#include "psim/platform/config.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/runtime.hpp"

namespace psim::service_template {

namespace {

constexpr auto kAwait = asio::as_tuple(asio::use_awaitable);

}  // namespace

Worker::Worker(const platform::runtime::Context& context)
    : context_(context),
      interval_(context.config().get<std::int64_t>("settings.interval_ms")),
      work_(context.config().get<std::int64_t>("settings.work_ms")),
      intake_timer_(context.executor()),
      idle_(context.executor()),
      processed_total_(&prometheus::BuildCounter()
                            .Name("psim_template_messages_processed_total")
                            .Help("Messages processed by the template worker")
                            .Register(context.metrics().registry())
                            .Add({})),
      in_flight_gauge_(&prometheus::BuildGauge()
                            .Name("psim_template_messages_in_flight")
                            .Help("Messages being processed")
                            .Register(context.metrics().registry())
                            .Add({})) {}

asio::awaitable<platform::Result<void>> Worker::start() {
  accepting_ = true;
  asio::co_spawn(context_.executor(), intake(), asio::detached);
  context_.logger().info("worker started",
                         {{"interval_ms", std::int64_t{interval_.count()}}, {"work_ms", std::int64_t{work_.count()}}});
  co_return platform::Result<void>{};
}

asio::awaitable<void> Worker::intake() {
  while (accepting_) {
    intake_timer_.expires_after(interval_);
    if (auto [ec] = co_await intake_timer_.async_wait(kAwait); ec || !accepting_) {
      co_return;
    }
    ++stats_.accepted;
    asio::co_spawn(context_.executor(), process(stats_.accepted), asio::detached);
  }
}

asio::awaitable<void> Worker::process(std::uint64_t sequence) {
  ++stats_.in_flight;
  in_flight_gauge_->Increment();
  auto span =
      context_.tracer("psim.service-template")->StartSpan("process message", {{"psim.message.sequence", sequence}});
  {
    const opentelemetry::trace::Scope scope(span);
    context_.logger().debug("message accepted", {{"sequence", sequence}});
  }
  asio::steady_timer work(context_.executor(), work_);
  co_await work.async_wait(kAwait);
  span->End();
  ++stats_.processed;
  processed_total_->Increment();
  in_flight_gauge_->Decrement();
  if (--stats_.in_flight == 0 && !accepting_) {
    idle_.cancel();
  }
}

asio::awaitable<void> Worker::drain() {
  accepting_ = false;
  intake_timer_.cancel();
  if (stats_.in_flight > 0) {
    idle_.expires_at(asio::steady_timer::time_point::max());
    co_await idle_.async_wait(kAwait);
  }
  context_.logger().info(
      "worker drained",
      {{"accepted", stats_.accepted}, {"processed", stats_.processed}, {"in_flight", stats_.in_flight}});
}

void Worker::reconfigure(const platform::config::Config& config) {
  interval_ = std::chrono::milliseconds(config.get<std::int64_t>("settings.interval_ms"));
  context_.logger().info("worker reconfigured", {{"interval_ms", std::int64_t{interval_.count()}}});
}

}  // namespace psim::service_template
