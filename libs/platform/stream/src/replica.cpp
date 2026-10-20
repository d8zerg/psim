#include "psim/platform/stream/replica.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::platform::stream {
Replica::Replica(std::unique_ptr<kafka::Consumer> source, Apply apply)
    : source_(std::move(source)), apply_(std::move(apply)) {}

asio::awaitable<Result<std::unique_ptr<Replica>>> Replica::open(asio::any_io_executor executor,
                                                                kafka::ClientOptions options,
                                                                kafka::Dependencies dependencies,
                                                                std::vector<std::string> topics, Apply apply,
                                                                std::vector<std::int32_t> partitions) {
  auto consumer = kafka::Consumer::create(executor,
                                          {.client = std::move(options),
                                           .group_id = "replica-" + uuid7(),
                                           .topics = std::move(topics),
                                           .group_protocol = "classic",
                                           .manual = true},
                                          dependencies);
  if (!consumer) {
    co_return std::unexpected(consumer.error());
  }
  auto bounds = co_await (*consumer)->end_offsets(std::chrono::seconds(10));
  if (!bounds) {
    co_return std::unexpected(bounds.error());
  }
  if (!partitions.empty()) {
    std::erase_if(*bounds, [&partitions](const auto& entry) {
      return !std::ranges::contains(partitions, entry.first.partition);
    });
  }
  auto starts = *bounds;
  for (auto& [partition, offset] : starts) {
    offset = 0;
  }
  if (auto assigned = (*consumer)->assign(starts); !assigned) {
    co_return std::unexpected(assigned.error());
  }
  auto replica = std::unique_ptr<Replica>(new Replica(std::move(*consumer), std::move(apply)));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (true) {
    const auto& positions = replica->source_->positions();
    const bool caught_up = std::ranges::all_of(*bounds, [&positions](const auto& entry) {
      const auto current = positions.find(entry.first);
      return current != positions.end() && current->second >= entry.second;
    });
    if (caught_up) {
      co_return replica;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      co_return fail(ErrorCode::kCommonUnavailable, "compacted replica bootstrap timed out");
    }
    if (auto applied = co_await replica->poll(std::chrono::milliseconds(100)); !applied) {
      co_return std::unexpected(applied.error());
    }
  }
}

asio::awaitable<Result<void>> Replica::poll(std::chrono::milliseconds wait) {
  auto messages = co_await source_->poll(4096, wait);
  if (!messages) {
    co_return std::unexpected(messages.error());
  }
  for (const auto& message : *messages) {
    auto applied = apply_(message);
    if (!applied) {
      co_return applied;
    }
  }
  co_return Result<void>{};
}

asio::awaitable<Result<void>> Replica::update() {
  co_return co_await poll(std::chrono::milliseconds(0));
}

asio::awaitable<void> Replica::close() {
  co_await source_->close();
}
}  // namespace psim::platform::stream
