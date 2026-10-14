#include "psim/sim/pipeline.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "psim/sim/broker.hpp"

namespace {

using psim::sim::Action;
using psim::sim::Options;
using psim::sim::Point;
using psim::sim::run_pipeline;
using psim::sim::Stage;

TEST(SimPipeline, ConvergesWithDuplicationReorderingAndSeededFailures) {
  std::array<std::size_t, 6> actions{};
  std::size_t restarts = 0;
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const auto report = run_pipeline({.seed = seed});
    ASSERT_TRUE(report.ok) << "seed=" << seed << " " << report.error;
    EXPECT_EQ(report.expected, 24U);
    EXPECT_EQ(report.normalized, report.expected);
    EXPECT_EQ(report.delivered, report.expected);
    EXPECT_GE(report.publications, report.expected);
    EXPECT_LT(report.elapsed, std::chrono::seconds{30});
    restarts += report.restarts;
    for (std::size_t index = 0; index < actions.size(); ++index) {
      actions.at(index) += report.actions.at(index);
    }
  }
  EXPECT_GT(restarts, 0U);
  for (const auto count : actions) {
    EXPECT_GT(count, 0U);
  }
}

TEST(SimPipeline, SeedReproducesTheEntireTraceAndChangesTheSchedule) {
  const auto first = run_pipeline({.seed = 42});
  const auto replay = run_pipeline({.seed = 42});
  const auto another = run_pipeline({.seed = 43});
  ASSERT_TRUE(first.ok) << first.error;
  ASSERT_TRUE(replay.ok) << replay.error;
  ASSERT_TRUE(another.ok) << another.error;
  EXPECT_EQ(first.trace, replay.trace);
  EXPECT_EQ(first.fingerprint, replay.fingerprint);
  // Pin the schedule of model v1, so an algorithm change cannot silently invalidate old seeds.
  EXPECT_EQ(psim::sim::kModelVersion, 1U);
  EXPECT_EQ(first.fingerprint, 8052093670133731658ULL);
  EXPECT_EQ(first.handlers, replay.handlers);
  EXPECT_EQ(first.elapsed, replay.elapsed);
  EXPECT_NE(first.trace, another.trace);
  EXPECT_NE(first.fingerprint, another.fingerprint);
}

struct FailureWindow {
  Stage stage;
  Point point;
};

class SimCrashWindow : public testing::TestWithParam<FailureWindow> {};

TEST_P(SimCrashWindow, RestartsFromDurableStateWithoutLostOrRepeatedEffects) {
  const auto target = GetParam();
  int hits = 0;
  const auto report =
      run_pipeline({.seed = 7, .events = 8}, [target, &hits](Stage stage, Point point, std::size_t, std::size_t) {
        if (stage == target.stage && point == target.point && hits < 3) {
          ++hits;
          return Action::kCrash;
        }
        return Action::kPass;
      });
  ASSERT_TRUE(report.ok) << report.error;
  EXPECT_EQ(hits, 3);
  EXPECT_EQ(report.restarts, 3U);
  EXPECT_EQ(report.normalized, report.expected);
  EXPECT_EQ(report.delivered, report.expected);
  if (target.stage == Stage::kRelay && target.point != Point::kPublish) {
    EXPECT_GT(report.publications, report.expected);
  }
}

INSTANTIATE_TEST_SUITE_P(DurableBoundaries, SimCrashWindow,
                         testing::Values(FailureWindow{Stage::kKafka, Point::kRead},
                                         FailureWindow{Stage::kKafka, Point::kBeforeCommit},
                                         FailureWindow{Stage::kKafka, Point::kAfterCommit},
                                         FailureWindow{Stage::kDatabase, Point::kBeforeCommit},
                                         FailureWindow{Stage::kDatabase, Point::kAfterCommit},
                                         FailureWindow{Stage::kRelay, Point::kPublish},
                                         FailureWindow{Stage::kRelay, Point::kAfterPublish},
                                         FailureWindow{Stage::kRelay, Point::kAcknowledge},
                                         FailureWindow{Stage::kSink, Point::kBeforeCommit},
                                         FailureWindow{Stage::kSink, Point::kAfterCommit}));

TEST(SimPipeline, LostReadResponsesAndUnavailableStorageRetryDurableInput) {
  int drops = 0;
  int unavailable = 0;
  const auto report = run_pipeline({}, [&drops, &unavailable](Stage, Point point, std::size_t, std::size_t) {
    if (point == Point::kRead && drops < 4) {
      ++drops;
      return Action::kDrop;
    }
    if (point == Point::kBeforeCommit && unavailable < 4) {
      ++unavailable;
      return Action::kUnavailable;
    }
    return Action::kPass;
  });
  ASSERT_TRUE(report.ok) << report.error;
  EXPECT_EQ(drops, 4);
  EXPECT_EQ(unavailable, 4);
}

TEST(SimPipeline, RepeatedPublishesAreVisibleInTheLogButDedupedAtTheSink) {
  const auto report = run_pipeline({}, [](Stage, Point point, std::size_t, std::size_t) {
    return point == Point::kPublish ? Action::kDuplicate : Action::kPass;
  });
  ASSERT_TRUE(report.ok) << report.error;
  EXPECT_EQ(report.publications, report.expected * 2);
  EXPECT_EQ(report.delivered, report.expected);
}

TEST(SimPipeline, IndependentOracleRejectsCorruptionAndCollidingOutputIds) {
  const auto corrupt = run_pipeline({}, {}, [](psim::sim::Message message) {
    message.payload = "corrupted";
    return message;
  });
  EXPECT_FALSE(corrupt.ok);
  EXPECT_NE(corrupt.error.find("corrupted"), std::string::npos);
  const auto duplicate = run_pipeline({}, {}, [](psim::sim::Message message) {
    message.id = "same-id";
    return message;
  });
  EXPECT_FALSE(duplicate.ok);
  EXPECT_NE(duplicate.error.find("duplicate"), std::string::npos);
}

TEST(SimPipeline, PersistentFailuresFailWithTheSameSeedInsteadOfHanging) {
  const auto always_down = [](Stage, Point, std::size_t, std::size_t) {
    return Action::kUnavailable;
  };
  const Options options{.seed = 99, .budget = {.handlers = 200}};
  const auto failure = run_pipeline(options, always_down);
  const auto replay = run_pipeline(options, always_down);
  EXPECT_FALSE(failure.ok);
  EXPECT_NE(failure.error.find("budget"), std::string::npos);
  EXPECT_EQ(failure.trace, replay.trace);
  EXPECT_EQ(failure.fingerprint, replay.fingerprint);
}

TEST(SimPipeline, SupportsSinglePartitionEmptyPartitionsAndNoFaults) {
  for (const auto partitions : std::array<std::size_t, 3>{1, 3, 8}) {
    const auto report = run_pipeline({.events = 2, .partitions = partitions, .fault_decisions = 0});
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_EQ(report.restarts, 0U);
    EXPECT_EQ(report.normalized, 2U);
    EXPECT_EQ(report.delivered, 2U);
  }
}

TEST(SimPipeline, RejectsInvalidOptionsAndReportsProcessorExceptions) {
  EXPECT_FALSE(run_pipeline({.events = 0}).ok);
  EXPECT_FALSE(run_pipeline({.events = 10001}).ok);
  EXPECT_FALSE(run_pipeline({.partitions = 0}).ok);
  EXPECT_FALSE(run_pipeline({.partitions = 65}).ok);
  const auto report = run_pipeline(
      {}, {}, [](const psim::sim::Message&) -> psim::sim::Message { throw std::runtime_error("broken processor"); });
  EXPECT_FALSE(report.ok);
  EXPECT_EQ(report.error, "broken processor");
}

}  // namespace
