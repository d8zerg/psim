#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <utility>
#include <vector>

#include "psim/platform/clickhouse/client.hpp"
#include "psim/platform/kafka/client.hpp"
#include "repository.hpp"
#include "rules.hpp"

namespace psim::correlation::management {
struct HistoricalInput {
  std::vector<rules::Event> events;
  rules::Context context;
};

Json snapshot(const HistoricalInput& input);
p::Result<HistoricalInput> restore_input(const Json& input);

/// Historical readers expose no producer, offset commit or administrative storage operations.
class History {
 public:
  History(asio::any_io_executor executor, p::kafka::ClientOptions options, p::kafka::Dependencies dependencies,
          p::clickhouse::Pool* clickhouse)
      : executor_(std::move(executor)),
        options_(std::move(options)),
        dependencies_(dependencies),
        clickhouse_(clickhouse) {}

  asio::awaitable<p::Result<HistoricalInput>> read(Job job);

  void stop() { stopping_ = true; }

 private:
  asio::any_io_executor executor_;
  p::kafka::ClientOptions options_;
  p::kafka::Dependencies dependencies_;
  p::clickhouse::Pool* clickhouse_;
  bool stopping_ = false;
};
}  // namespace psim::correlation::management
