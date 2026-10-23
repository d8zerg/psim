#include "console_replay.hpp"

#include <boost/asio/this_coro.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "console_store.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::projection {
namespace p = platform;
namespace asio = boost::asio;

std::vector<std::string> console_topics() {
  return {"psim.catalog.v1",           "psim.config.v1",       "psim.incidents.events.v1",
          "psim.events.normalized.v1", "psim.ingest.state.v1", "psim.response.events.v1"};
}

asio::awaitable<p::Result<void>> console_catch_up(ConsoleStore* store, p::kafka::Consumer* source,
                                                  p::kafka::Offsets targets, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!std::ranges::all_of(targets, [source](const auto& entry) {
    const auto found = source->positions().find(entry.first);
    return found != source->positions().end() && found->second >= entry.second;
  })) {
    if (std::chrono::steady_clock::now() >= deadline) {
      co_return p::fail(p::ErrorCode::kCommonDeadlineExceeded, "console replay did not reach captured offsets");
    }
    auto messages = co_await source->poll(64, std::chrono::milliseconds(2));
    if (!messages) {
      co_return std::unexpected(messages.error());
    }
    for (const auto& message : *messages) {
      const p::kafka::TopicPartition partition{std::string(message.topic()), message.partition()};
      if (targets.contains(partition) && message.offset() < targets.at(partition)) {
        auto applied = co_await store->apply(&message);
        if (!applied) {
          co_return applied;
        }
      }
    }
  }
  co_return co_await store->advance(targets);
}

asio::awaitable<p::Result<std::string>> console_rebuild(p::pg::Pool* pool, p::kafka::ClientOptions options,
                                                        p::kafka::Dependencies dependencies,
                                                        std::chrono::milliseconds timeout) {
  ConsoleStore initial(pool, "initial");
  auto initialized = co_await initial.initialize();
  if (!initialized) {
    co_return std::unexpected(initialized.error());
  }
  auto guard = co_await pool->acquire();
  if (!guard) {
    co_return std::unexpected(guard.error());
  }
  auto locked = co_await (*guard)->query(
      {.sql = "SELECT pg_try_advisory_lock($1)", .parameters = {p::pg::Parameter::integer(kConsoleWriterLock)}});
  if (!locked || locked->value(0, 0) != "t") {
    co_return p::fail(p::ErrorCode::kCommonUnavailable, "stop the console writer before rebuilding");
  }
  const auto generation = p::stream::uuid7();
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines): closure is awaited before its owner returns.
  auto execute = [&]() -> asio::awaitable<p::Result<std::string>> {
    const auto executor = co_await asio::this_coro::executor;
    auto source = p::kafka::Consumer::create(executor,
                                             {.client = std::move(options),
                                              .group_id = "console-rebuild-" + generation,
                                              .topics = console_topics(),
                                              .manual = true},
                                             dependencies);
    if (!source) {
      co_return std::unexpected(source.error());
    }
    auto bounds = co_await (*source)->offset_bounds(std::chrono::seconds(10));
    if (!bounds) {
      co_return std::unexpected(bounds.error());
    }
    auto active = co_await initial.active();
    if (!active) {
      co_return std::unexpected(active.error());
    }
    ConsoleStore working(pool, *active);
    auto positions = co_await working.positions();
    if (!positions) {
      co_return std::unexpected(positions.error());
    }
    const auto targets = positions->empty() ? bounds->end : *positions;
    auto starts = bounds->begin;
    for (const auto& [partition, offset] : targets) {
      if (!starts.contains(partition) || starts.at(partition) > offset || bounds->end.at(partition) < offset) {
        co_return p::fail(p::ErrorCode::kCommonUnavailable,
                          "retained Kafka offsets cannot reconstruct the active generation");
      }
    }
    auto assigned = (*source)->assign(starts);
    if (!assigned) {
      co_return std::unexpected(assigned.error());
    }
    ConsoleStore fresh(pool, generation);
    fresh.writer(&**guard);
    auto created = co_await fresh.positions();
    if (!created) {
      co_return std::unexpected(created.error());
    }
    auto replayed = co_await console_catch_up(&fresh, source->get(), targets, timeout);
    co_await (*source)->close();
    if (!replayed) {
      auto removed = co_await fresh.discard();
      (void)removed;
      co_return std::unexpected(replayed.error());
    }
    auto activated = co_await fresh.activate(true);
    if (!activated) {
      auto removed = co_await fresh.discard();
      (void)removed;
      co_return std::unexpected(activated.error());
    }
    co_return generation;
  };
  auto result = co_await execute();
  (*guard)->close();
  co_return result;
}
}  // namespace psim::projection
