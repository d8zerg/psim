#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>

#include <chrono>
#include <cstdint>
#include <string_view>

#include "psim/platform/config.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/runtime.hpp"

namespace psim::service_template {

namespace asio = boost::asio;

/// Sample component: takes a message every interval_ms and processes it for work_ms. It shows the
/// contract of every component: metrics, a span per message, logs, readiness and a drain that
/// finishes the messages in progress before the process exits.
class Worker final : public platform::runtime::Component {
 public:
  explicit Worker(const platform::runtime::Context& context);

  [[nodiscard]] std::string_view name() const override { return "worker"; }

  asio::awaitable<platform::Result<void>> start() override;

  [[nodiscard]] bool ready() const override { return accepting_; }

  asio::awaitable<void> drain() override;
  void reconfigure(const platform::config::Config& config) override;

  struct Stats {
    std::uint64_t accepted = 0;
    std::uint64_t processed = 0;
    std::uint64_t in_flight = 0;
  };

  [[nodiscard]] Stats stats() const noexcept { return stats_; }

 private:
  asio::awaitable<void> intake();
  asio::awaitable<void> process(std::uint64_t sequence);

  const platform::runtime::Context& context_;
  std::chrono::milliseconds interval_;
  std::chrono::milliseconds work_;
  bool accepting_ = false;
  Stats stats_;
  asio::steady_timer intake_timer_;
  asio::steady_timer idle_;  // cancelled when the last message in progress completes
  prometheus::Counter* processed_total_;
  prometheus::Gauge* in_flight_gauge_;
};

}  // namespace psim::service_template
