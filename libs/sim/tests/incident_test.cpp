#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <utility>

#include "psim/domain/incident/model.hpp"
#include "psim/sim/broker.hpp"
#include "psim/sim/store.hpp"

namespace psim::sim {
TEST(IncidentSimulation, InboxAndGroupingSurviveCrashesAndRepeatedPublication) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded simulation reads a fixed environment.
  const auto* runs_text = std::getenv("PSIM_INCIDENT_SIM_RUNS");
  const auto runs = runs_text ? std::stoul(runs_text) : 200;
  for (std::size_t seed = 1; seed <= runs; ++seed) {
    SCOPED_TRACE(std::to_string(seed));
    std::mt19937 random(static_cast<std::uint32_t>(seed));
    Broker broker;
    broker.create_topic("signals", 1);
    Store store;
    std::map<std::string, std::int64_t> windows;
    std::map<std::string, std::size_t> counts;
    for (std::size_t index = 0; index < 40; ++index) {
      const Message signal{"tenant", std::to_string(index), std::to_string(index / 10)};
      broker.append("signals", 0, signal);
      if (random() % 2 == 0) {
        broker.append("signals", 0, signal);
      }
    }
    std::size_t cursor = 0;
    while (cursor < broker.size("signals", 0)) {
      const auto message = broker.read("signals", 0, cursor).value_or(Message{});
      ASSERT_FALSE(message.id.empty());
      auto tx = store.begin();
      auto candidate_windows = windows;
      auto candidate_counts = counts;
      if (Store::apply(tx, message)) {
        const auto at = static_cast<std::int64_t>(std::stoll(message.id));
        auto& until = candidate_windows[message.payload];
        if (!domain::incident::accepts_signal({.active = candidate_counts[message.payload] != 0, .until = until}, at)) {
          until = at + 4;
          ++candidate_counts[message.payload];
        }
      }
      if (random() % 4 == 0) {
        continue;
      }  // Crash before the durable transaction commits.
      ASSERT_TRUE(store.commit(std::move(tx)));
      windows = std::move(candidate_windows);
      counts = std::move(candidate_counts);
      if (random() % 4 != 0) {
        ++cursor;
      }  // Lost offset acknowledgement repeats the same signal.
    }
    EXPECT_EQ(store.effects().size(), 40);
    EXPECT_EQ(counts.size(), 4);
    for (const auto& [group, count] : counts) {
      (void)group;
      EXPECT_EQ(count, 2);
    }
    while (const auto pending = store.pending()) {
      broker.append("signals", 0, store.entry(*pending).message);
      if (random() % 3 == 0) {
        broker.append("signals", 0, store.entry(*pending).message);
      }
      store.acknowledge(*pending);
    }
    for (const auto& [identity, message] : store.effects()) {
      (void)identity;
      auto tx = store.begin();
      EXPECT_FALSE(Store::apply(tx, message));
    }
  }
}
}  // namespace psim::sim
