#include "psim/platform/log.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using psim::platform::log::append_escaped;
using psim::platform::log::Level;
using psim::platform::log::Logger;
using psim::platform::log::LoggerOptions;
using psim::platform::log::MemorySink;
using psim::platform::log::parse_level;
using psim::platform::log::to_string;
using psim::platform::log::TraceIds;

LoggerOptions options() {
  LoggerOptions o;
  o.service = "normalizer";
  o.instance = "normalizer-0";
  o.now = [] {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(1'790'000'000'123));
  };
  return o;
}

TEST(Logger, WritesOneJsonObjectPerRecord) {
  auto sink = std::make_shared<MemorySink>();
  Logger logger(options(), sink);

  logger.info("batch committed", {{"partition", std::int64_t{3}},
                                  {"offset", std::uint64_t{42}},
                                  {"lag_ratio", 0.5},
                                  {"rebalance", false},
                                  {"topic", std::string_view("psim.events.normalized.v1")}});

  ASSERT_EQ(sink->lines().size(), 1U);
  EXPECT_EQ(sink->lines()[0],
            R"({"ts":"2026-09-21T14:13:20.123Z","level":"info","service":"normalizer","instance":"normalizer-0",)"
            R"("msg":"batch committed","partition":3,"offset":42,"lag_ratio":0.5,"rebalance":false,)"
            R"("topic":"psim.events.normalized.v1"})");
}

TEST(Logger, AddsTheActiveTraceContext) {
  auto sink = std::make_shared<MemorySink>();
  auto o = options();
  o.trace_context = [] {
    return std::optional<TraceIds>(TraceIds{"4bf92f3577b34da6a3ce929d0e0e4736", "00f067aa0ba902b7"});
  };
  Logger logger(std::move(o), sink);

  logger.warn("slow consumer");

  EXPECT_NE(sink->lines()[0].find(R"("trace_id":"4bf92f3577b34da6a3ce929d0e0e4736","span_id":"00f067aa0ba902b7")"),
            std::string::npos);
}

TEST(Logger, FiltersByALevelThatChangesAtRunTime) {
  auto sink = std::make_shared<MemorySink>();
  Logger logger(options(), sink);

  logger.debug("hidden");
  logger.set_level(Level::kDebug);
  logger.debug("shown");
  logger.set_level(Level::kError);
  logger.warn("hidden");
  logger.error("shown");

  ASSERT_EQ(sink->lines().size(), 2U);
  EXPECT_EQ(logger.level(), Level::kError);
}

TEST(Logger, EscapesEveryStringItWrites) {
  std::string out;
  append_escaped(out, "a\"b\\c\nd\re\tf\x01g");
  EXPECT_EQ(out, R"(a\"b\\c\nd\re\tf\u0001g)");
}

TEST(Levels, RoundTripThroughTheirNames) {
  for (const auto level : {Level::kTrace, Level::kDebug, Level::kInfo, Level::kWarn, Level::kError}) {
    EXPECT_EQ(parse_level(to_string(level)), level);
  }
  EXPECT_FALSE(parse_level("verbose").has_value());
}

}  // namespace
