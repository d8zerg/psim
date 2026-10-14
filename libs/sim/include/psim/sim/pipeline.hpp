#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "psim/sim/broker.hpp"
#include "psim/sim/scheduler.hpp"

namespace psim::sim {

inline constexpr std::uint32_t kModelVersion = 1;

enum class Stage : std::uint8_t { kKafka, kDatabase, kRelay, kSink };
enum class Point : std::uint8_t { kRead, kBeforeCommit, kAfterCommit, kPublish, kAfterPublish, kAcknowledge };
enum class Action : std::uint8_t { kPass, kDrop, kDuplicate, kDelay, kCrash, kUnavailable };

struct Options {
  std::uint64_t seed = 1;
  std::size_t events = 24;
  std::size_t partitions = 3;
  // Faults stop after this many decisions, so a healthy suffix can establish convergence.
  std::size_t fault_decisions = 120;
  RunBudget budget{};
};

struct Report {
  Options options;
  bool ok = false;
  std::string error;
  std::size_t expected = 0;
  std::size_t delivered = 0;
  std::size_t normalized = 0;
  std::size_t publications = 0;
  std::size_t restarts = 0;
  std::size_t handlers = 0;
  platform::async::Duration elapsed{};
  std::array<std::size_t, 6> actions{};
  std::uint64_t fingerprint = 14695981039346656037ULL;
  std::vector<std::string> trace;
};

// Explicit scripts permit targeted regression scenarios. A script replaces seeded faults.
// The last argument is the global decision index, starting at zero.
using FaultScript = std::function<Action(Stage, Point, std::size_t, std::size_t)>;
// Adapter hook for a future deterministic domain processor. The reference oracle expects identity.
using Transform = std::function<Message(Message)>;

/// Reference delivery pipeline: Kafka transaction + changelog -> inbox/outbox -> deduped sink.
/// A seed reproduces every decision for the same model version and Options. Does not run services.
[[nodiscard]] Report run_pipeline(Options options = {}, FaultScript script = {}, Transform transform = {});

}  // namespace psim::sim
