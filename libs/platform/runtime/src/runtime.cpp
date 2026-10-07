#include "psim/platform/runtime.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers): SIGHUP is POSIX, not in <csignal>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "admin_server.hpp"
#include "psim/platform/build_info.hpp"
#include "psim/platform/config.hpp"
#include "psim/platform/log.hpp"
#include "psim/platform/metrics.hpp"
#include "psim/platform/tracing.hpp"

namespace psim::platform::runtime {

namespace {

constexpr int kExitOk = 0;
constexpr int kExitStartFailed = 1;
constexpr int kExitDrainTimeout = 3;

std::string host_name() {
  std::array<char, 256> buffer{};
  if (::gethostname(buffer.data(), buffer.size() - 1) != 0) {
    return "unknown";
  }
  return {buffer.data()};
}

log::LoggerOptions logger_options(const std::string& service, const std::string& instance,
                                  const config::Config& config) {
  log::LoggerOptions options;
  options.service = service;
  options.instance = instance;
  options.level = log::parse_level(config.get<std::string>("log.level")).value_or(log::Level::kInfo);
  options.trace_context = observability::current_trace_ids;
  return options;
}

}  // namespace

struct Runtime::Impl {
  Impl(ServiceDefinition service_definition, config::Schema configuration_schema, config::Config configuration,
       config::Sources configuration_sources, std::shared_ptr<log::Sink> sink)
      : service(std::move(service_definition)),
        schema(std::move(configuration_schema)),
        config(std::move(configuration)),
        sources(std::move(configuration_sources)),
        instance(config.has("service.instance") ? config.get<std::string>("service.instance") : host_name()),
        logger(logger_options(service.name, instance, config), std::move(sink)),
        signals(io, SIGINT, SIGTERM, SIGHUP),
        ready_gauge(&prometheus::BuildGauge()
                         .Name("psim_runtime_ready")
                         .Help("1 when the service reports ready at /health/ready")
                         .Register(metrics.registry())
                         .Add({})),
        reloads(&prometheus::BuildCounter()
                     .Name("psim_runtime_config_reloads_total")
                     .Help("Configuration reloads by result")
                     .Register(metrics.registry())),
        drain_timeouts(&prometheus::BuildCounter()
                            .Name("psim_runtime_drain_timeouts_total")
                            .Help("Shutdowns whose drain exceeded shutdown.drain_timeout_ms")
                            .Register(metrics.registry())
                            .Add({})) {
    const auto build = build_info();
    prometheus::BuildGauge()
        .Name("psim_runtime_info")
        .Help("Service build and instance")
        .Register(metrics.registry())
        .Add({{"service", service.name},
              {"instance", instance},
              {"version", std::string(build.version)},
              {"commit", std::string(build.git_commit)}})
        .Set(1);
    prometheus::BuildGauge()
        .Name("psim_runtime_start_time_seconds")
        .Help("Start time of the process, Unix seconds")
        .Register(metrics.registry())
        .Add({})
        .Set(static_cast<double>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count()));
  }

  [[nodiscard]] bool ready() const {
    if (!started || stopping) {
      return false;
    }
    return std::ranges::all_of(components, [](const auto& c) { return c->ready(); });
  }

  detail::AdminResponse handle(std::string_view target) const {
    using nlohmann::json;
    if (target == "/health/live") {
      return {.status = 200, .content_type = "application/json", .body = R"({"status":"live"})"};
    }
    if (target == "/health/ready") {
      json body = {{"status", ready() ? "ready" : "not_ready"}, {"started", started}, {"stopping", stopping}};
      for (const auto& component : components) {
        body["components"][std::string(component->name())] = component->ready();
      }
      return {.status = ready() ? 200U : 503U, .content_type = "application/json", .body = body.dump()};
    }
    if (target == "/metrics") {
      return {.status = 200, .content_type = "text/plain; version=0.0.4; charset=utf-8", .body = metrics.expose()};
    }
    if (target == "/info") {
      const auto build = build_info();
      const json body = {{"service", service.name},    {"instance", instance},       {"version", build.version},
                         {"commit", build.git_commit}, {"compiler", build.compiler}, {"build_type", build.build_type}};
      return {.status = 200, .content_type = "application/json", .body = body.dump()};
    }
    return {.status = 404, .content_type = "application/json", .body = R"({"status":"not_found"})"};
  }

  void watch_signals() {
    signals.async_wait([this](const boost::system::error_code& ec, int signal) {
      if (ec) {
        return;  // cancelled on shutdown
      }
      if (signal == SIGHUP) {
        reload();
      } else {
        logger.info("stop signal received", {{"signal", std::int64_t{signal}}});
        asio::co_spawn(io, shutdown(), asio::detached);
      }
      watch_signals();
    });
  }

  asio::awaitable<void> start_all() {
    for (const auto& component : components) {
      if (stopping) {
        co_return;
      }
      auto started_component = co_await component->start();
      if (!started_component) {
        logger.error("component failed to start", {{"component", component->name()},
                                                   {"code", started_component.error().name()},
                                                   {"error", std::string_view(started_component.error().message())}});
        exit_code = kExitStartFailed;
        co_await shutdown();
        co_return;
      }
    }
    started = true;
    ready_gauge->Set(ready() ? 1 : 0);
    logger.info("service started", {{"admin_port", std::int64_t{admin_port.load()}},
                                    {"components", static_cast<std::int64_t>(components.size())}});
  }

  asio::awaitable<void> shutdown() {
    using namespace asio::experimental::awaitable_operators;  // NOLINT(google-build-using-namespace): operators
    if (stopping) {
      co_return;
    }
    stopping = true;
    ready_gauge->Set(0);
    logger.info("service stopping: draining");
    const auto timeout = std::chrono::milliseconds(config.get<std::int64_t>("shutdown.drain_timeout_ms"));
    asio::steady_timer deadline(io, timeout);
    for (const auto& component : std::views::reverse(components)) {
      auto outcome = co_await (component->drain() || deadline.async_wait(asio::use_awaitable));
      if (outcome.index() == 1) {
        logger.error("drain timeout: work in progress may be lost",
                     {{"component", component->name()}, {"timeout_ms", std::int64_t{timeout.count()}}});
        drain_timeouts->Increment();
        exit_code = kExitDrainTimeout;
        break;
      }
    }
    if (admin) {
      admin->close();
    }
    boost::system::error_code ignored;
    (void)signals.cancel(ignored);
    logger.info("service stopped", {{"exit_code", std::int64_t{exit_code}}});
    io.stop();
  }

  void reload() {
    auto updated = config::load(schema, sources);
    if (!updated) {
      for (const auto& problem : updated.error()) {
        logger.error("configuration reload rejected",
                     {{"path", std::string_view(problem.path)}, {"problem", std::string_view(problem.message)}});
      }
      reloads->Add({{"result", "rejected"}}).Increment();
      return;
    }
    const auto plan = config::plan_reload(schema, config, *updated);
    for (const auto& path : plan.restart_required) {
      logger.warn("setting changed; takes effect after a restart", {{"path", std::string_view(path)}});
    }
    config = std::move(*updated);
    if (const auto level = log::parse_level(config.get<std::string>("log.level"))) {
      logger.set_level(*level);
    }
    for (const auto& component : components) {
      component->reconfigure(config);
    }
    reloads->Add({{"result", "applied"}}).Increment();
    logger.info("configuration reloaded",
                {{"applied", static_cast<std::int64_t>(plan.apply.size())},
                 {"restart_required", static_cast<std::int64_t>(plan.restart_required.size())}});
  }

  int run() {
    tracing = std::make_unique<observability::Tracing>(
        observability::TracingOptions{.service = service.name,
                                      .version = std::string(build_info().version),
                                      .instance = instance,
                                      .otlp_endpoint = config.get<std::string>("telemetry.otlp_endpoint"),
                                      .sampling_ratio = config.get<double>("telemetry.sampling_ratio")});
    context = std::make_unique<Context>(io.get_executor(), logger, metrics, *tracing, config);

    auto created = service.create(*context);
    if (!created) {
      logger.error("service failed to initialize",
                   {{"code", created.error().name()}, {"error", std::string_view(created.error().message())}});
      return kExitStartFailed;
    }
    components = std::move(*created);

    admin = std::make_unique<detail::AdminServer>(io.get_executor(),
                                                  [this](std::string_view target) { return handle(target); });
    const auto listening = admin->listen(config.get<std::string>("admin.listen"));
    if (!listening) {
      logger.error("admin endpoint failed", {{"error", std::string_view(listening.error().message())}});
      return kExitStartFailed;
    }
    admin_port = *listening;
    logger.info("service starting", {{"version", build_info().version},
                                     {"commit", build_info().git_commit},
                                     {"config", std::string_view(config_summary())}});
    watch_signals();
    asio::co_spawn(io, start_all(), asio::detached);
    io.run();
    tracing->flush();
    return exit_code;
  }

  // The document never holds secrets (config::Config keeps them apart), so it is safe to log.
  [[nodiscard]] std::string config_summary() const { return config.document().dump(); }

  ServiceDefinition service;
  config::Schema schema;
  config::Config config;
  config::Sources sources;
  std::string instance;
  log::Logger logger;
  observability::Metrics metrics;
  asio::io_context io{1};
  asio::signal_set signals;
  std::unique_ptr<observability::Tracing> tracing;
  std::unique_ptr<Context> context;
  Components components;
  std::unique_ptr<detail::AdminServer> admin;
  std::atomic<int> admin_port{-1};
  bool started = false;
  bool stopping = false;
  int exit_code = kExitOk;
  prometheus::Gauge* ready_gauge;
  prometheus::Family<prometheus::Counter>* reloads;
  prometheus::Counter* drain_timeouts;
};

Runtime::Runtime(ServiceDefinition service, config::Schema schema, config::Config config, config::Sources sources,
                 std::shared_ptr<log::Sink> sink)
    : impl_(std::make_unique<Impl>(std::move(service), std::move(schema), std::move(config), std::move(sources),
                                   std::move(sink))) {}

Runtime::~Runtime() = default;

int Runtime::run() {
  return impl_->run();
}

void Runtime::request_stop() {
  asio::post(impl_->io, [impl = impl_.get()] { asio::co_spawn(impl->io, impl->shutdown(), asio::detached); });
}

void Runtime::request_reload() {
  asio::post(impl_->io, [impl = impl_.get()] { impl->reload(); });
}

std::optional<unsigned short> Runtime::admin_port() const noexcept {
  const int port = impl_->admin_port.load();
  if (port < 0) {
    return std::nullopt;
  }
  return static_cast<unsigned short>(port);
}

}  // namespace psim::platform::runtime
