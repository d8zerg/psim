#include <gtest/gtest.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "live.hpp"
#include "live_support.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "psim/sim/broker.hpp"
#include "psim/sim/random.hpp"
#include "rules.hpp"

namespace psim::correlation::live {
namespace {
using namespace test;

std::uint64_t option(const char* name, std::uint64_t fallback) {
  const auto* text = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): single-threaded simulation, fixed environment
  if (text == nullptr) {
    return fallback;
  }
  const std::string_view value(text);
  std::uint64_t result = 0;
  const auto parsed = std::from_chars(value.begin(), value.end(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.end() || result == 0) {
    throw std::invalid_argument("invalid correlation simulation option");
  }
  return result;
}

class SimulationTrace {
 public:
  SimulationTrace(std::uint64_t seed, bool threshold) : seed_(seed), threshold_(threshold) {
    data_ << "model=1 seed=" << seed << " threshold=" << threshold << '\n';
  }

  SimulationTrace(const SimulationTrace&) = delete;
  SimulationTrace& operator=(const SimulationTrace&) = delete;
  SimulationTrace(SimulationTrace&&) = delete;
  SimulationTrace& operator=(SimulationTrace&&) = delete;

  ~SimulationTrace() {
    const auto* requested =
        std::getenv("PSIM_CORRELATION_SIM_TRACE");  // NOLINT(concurrency-mt-unsafe): fixed environment
    if (!::testing::Test::HasFailure() && (requested == nullptr || std::string_view(requested) != "true")) {
      return;
    }
    std::error_code error;
    std::filesystem::create_directories(".cache/sim", error);
    const auto path = std::string(".cache/sim/correlation-v1-seed-") + std::to_string(seed_)
                      + (threshold_ ? "-threshold.trace" : "-single.trace");
    std::ofstream file(path);
    file << data_.str();
    if (error || !file) {
      std::cerr << "cannot save correlation simulation trace\n";
    }
  }

  void record(std::size_t attempt, std::size_t offset, std::uint64_t fault, const State& state) {
    data_ << "attempt=" << attempt << " offset=" << offset << " fault=" << fault
          << " state_sha256=" << p::stream::sha256(state.SerializeAsString()) << '\n';
  }

  void decision(const std::string& id) { data_ << "signal=" << id << '\n'; }

 private:
  std::uint64_t seed_;
  bool threshold_;
  std::ostringstream data_;
};

TEST(CorrelationSimulation, CommittedSignalsMatchOracleAcrossCrashesAndReassignment) {
  const auto first = option("PSIM_CORRELATION_SIM_SEED", 1);
  const auto runs = option("PSIM_CORRELATION_SIM_RUNS", 200);
  if (runs > 100000 || first > std::numeric_limits<std::uint64_t>::max() - (runs - 1)) {
    throw std::invalid_argument("correlation simulation range exceeds its bounds");
  }
  for (std::uint64_t run = 0; run < runs; ++run) {
    const auto seed = first + run;
    SCOPED_TRACE(seed);
    for (const bool threshold : {false, true}) {
      SCOPED_TRACE(threshold);
      SimulationTrace trace(seed, threshold);
      sim::Random random(seed);
      sim::Broker broker;
      broker.create_topic("input", 1);
      broker.create_topic("signals", 1);
      broker.create_topic("state", 1);
      auto spec = rule();
      spec.mutable_allowed_lateness()->set_seconds(0);
      if (threshold) {
        spec.mutable_threshold()->set_count(2);
      }
      const auto compiled = compile(spec);
      std::vector<rules::Event> inputs;
      for (std::uint64_t index = 1; index <= 24; ++index) {
        inputs.push_back(event(1000 + (static_cast<std::int64_t>(index) * 10), index));
        broker.append("input", 0, {kTenant, inputs.back().envelope().message_id(), inputs.back().SerializeAsString()});
      }
      const auto oracle = rules::replay(compiled, inputs, {}, kTenant, 1000, 3000);
      ASSERT_TRUE(oracle);
      State local;
      std::size_t attempts = 0;
      std::size_t restarts = 0;
      auto restore = [&] {
        local.Clear();
        for (std::size_t offset = 0; offset < broker.size("state", 0); ++offset) {
          ASSERT_TRUE(local.ParseFromString(broker.read("state", 0, offset)->payload));
        }
        ++restarts;
      };
      while (broker.offset("engine", "input", 0) < inputs.size() && attempts++ < 1000) {
        const auto offset = broker.offset("engine", "input", 0);
        auto transaction = broker.begin("engine-owner");
        auto pending = local;
        auto result =
            advance(compiled, pending, &inputs[offset], {}, kTenant, 1100 + (static_cast<std::int64_t>(offset) * 10));
        ASSERT_TRUE(result);
        for (const auto& decision : result->decisions) {
          trace.decision(decision.id);
          transaction.publications.push_back(
              {"signals", 0, {kTenant, decision.id, decision.signal.SerializeAsString()}});
        }
        transaction.publications.push_back({"state", 0, {kTenant, "key", pending.SerializeAsString()}});
        transaction.offsets.push_back({"engine", "input", 0, offset + 1});
        const auto fault = attempts < 120 ? random.bounded(4) : 3;
        trace.record(attempts, offset, fault, pending);
        if (fault == 0) {
          const auto replacement = broker.begin("engine-owner");
          (void)replacement;
          EXPECT_FALSE(broker.commit(transaction));
          restore();
          continue;
        }
        ASSERT_TRUE(broker.commit(transaction));
        if (fault == 1) {
          // Kafka committed; the process died before writing its local cache.
          restore();
          continue;
        }
        local = std::move(pending);
        if (fault == 2) {
          // A different owner rebuilds its state after assignment.
          restore();
        }
      }
      EXPECT_LT(attempts, 1000);
      EXPECT_GT(restarts, 0);
      ASSERT_EQ(broker.offset("engine", "input", 0), inputs.size());
      std::set<std::string> expected;
      for (const auto& decision : oracle->decisions) {
        expected.insert(decision.id);
      }
      std::set<std::string> actual;
      for (std::size_t offset = 0; offset < broker.size("signals", 0); ++offset) {
        const auto message = broker.read("signals", 0, offset);
        ASSERT_TRUE(message);
        if (message) {
          EXPECT_TRUE(actual.insert(message->id).second);
        }
      }
      EXPECT_EQ(actual, expected);
      EXPECT_EQ(broker.size("signals", 0), threshold ? 12 : 24);
      if (::testing::Test::HasFailure()) {
        std::cerr << "replay: task correlation:sim SEED=" << seed << " RUNS=1 SAVE_TRACE=true\n";
        return;
      }
    }
  }
}
}  // namespace
}  // namespace psim::correlation::live
