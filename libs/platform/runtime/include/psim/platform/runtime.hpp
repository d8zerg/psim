#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/tracer.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/async/blocking_pool.hpp"
#include "psim/platform/async/clock.hpp"
#include "psim/platform/async/shards.hpp"
#include "psim/platform/config.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/log.hpp"
#include "psim/platform/metrics.hpp"
#include "psim/platform/tracing.hpp"

namespace psim::platform::runtime {

namespace asio = boost::asio;

/// What a component receives from the runtime.
/// Execution model of the process (ADR-016): data-path shards, time, the pool for blocking calls.
struct Execution {
  async::Shards* shards;
  async::Clock* clock;
  async::BlockingPool* blocking;
};

class Context {
 public:
  Context(asio::any_io_executor executor, log::Logger& logger, observability::Metrics& metrics,
          observability::Tracing& tracing, const config::Config& config, Execution execution)
      : executor_(std::move(executor)),
        logger_(&logger),
        metrics_(&metrics),
        tracing_(&tracing),
        config_(&config),
        execution_(execution) {}

  [[nodiscard]] const asio::any_io_executor& executor() const noexcept { return executor_; }

  [[nodiscard]] log::Logger& logger() const noexcept { return *logger_; }

  [[nodiscard]] observability::Metrics& metrics() const noexcept { return *metrics_; }

  [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer(std::string_view scope) const {
    return tracing_->tracer(scope);
  }

  /// Configuration at start; reloads arrive through Component::reconfigure.
  [[nodiscard]] const config::Config& config() const noexcept { return *config_; }

  /// Shards of the data path (runtime.shards); state bound to a shard is touched only on it.
  [[nodiscard]] async::Shards& shards() const noexcept { return *execution_.shards; }

  /// Time for timestamps, deadlines and waiting; ManualClock in tests.
  [[nodiscard]] async::Clock& clock() const noexcept { return *execution_.clock; }

  /// Pool for blocking calls (runtime.blocking_threads, runtime.blocking_tasks).
  [[nodiscard]] async::BlockingPool& blocking() const noexcept { return *execution_.blocking; }

 private:
  asio::any_io_executor executor_;
  log::Logger* logger_;
  observability::Metrics* metrics_;
  observability::Tracing* tracing_;
  const config::Config* config_;
  Execution execution_;
};

/// A part of a service with its own lifecycle. All methods run on the runtime's control executor;
/// data-path work is posted to Context::shards().
class Component {
 public:
  Component() = default;
  Component(const Component&) = delete;
  Component& operator=(const Component&) = delete;
  Component(Component&&) = delete;
  Component& operator=(Component&&) = delete;
  virtual ~Component() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;

  /// Start the work; an error aborts the start of the service.
  virtual asio::awaitable<Result<void>> start() = 0;

  /// Part of /health/ready: dependencies reachable, local state restored.
  [[nodiscard]] virtual bool ready() const { return true; }

  /// Stop taking new work and finish the work in progress (commit offsets, end transactions).
  /// The runtime bounds it by shutdown.drain_timeout_ms; work left after that may be lost.
  virtual asio::awaitable<void> drain() = 0;

  /// Settings marked x-psim-reload changed (SIGHUP); `config` is the new configuration.
  virtual void reconfigure(const config::Config& config) { (void)config; }
};

using Components = std::vector<std::unique_ptr<Component>>;

/// A service built on the runtime: its name, the JSON Schema of its `settings` section and a
/// factory of its components (started in order, drained in reverse order).
struct ServiceDefinition {
  std::string name;
  std::string settings_schema = R"({"type": "object", "additionalProperties": false})";
  std::function<Result<Components>(Context&)> create;
};

/// JSON Schema of the whole configuration: runtime sections plus the service `settings`.
[[nodiscard]] std::string configuration_schema(std::string_view settings_schema);

/// Runtime of one service process: admin HTTP endpoints, signals, lifecycle, graceful shutdown.
class Runtime {
 public:
  Runtime(ServiceDefinition service, config::Schema schema, config::Config config, config::Sources sources,
          std::shared_ptr<log::Sink> sink);
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) = delete;
  Runtime& operator=(Runtime&&) = delete;
  ~Runtime();

  /// Start, serve until a stop is requested (SIGTERM, SIGINT, request_stop), drain, return the exit code:
  /// 0 - clean stop, 1 - failed start, 3 - drain timeout (work in progress may be lost).
  int run();

  /// Thread-safe.
  void request_stop();
  /// Re-read the configuration sources and apply what changed (as SIGHUP). Thread-safe.
  void request_reload();

  /// Port of the admin HTTP server once it listens (useful with port 0 in tests).
  [[nodiscard]] std::optional<unsigned short> admin_port() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Entry point of a service: `int main(int argc, char** argv) { return run(argc, argv, definition()); }`.
/// Options: --config <file>, --check-config, --version.
int run(int argc, char** argv, const ServiceDefinition& service);

}  // namespace psim::platform::runtime
