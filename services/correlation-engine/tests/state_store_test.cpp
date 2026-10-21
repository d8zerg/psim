#include "state_store.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "live.hpp"
#include "live_support.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::correlation::live {
namespace {
TEST(CorrelationStateStore, ReopenAppliesCheckpointWithStateAndTombstone) {
  const auto directory = "/tmp/psim-correlation-test-" + p::stream::uuid7();
  const auto key = state_key(test::kTenant, test::rule(), "zone");
  auto opened = StateStore::open(directory);
  ASSERT_TRUE(opened);
  auto store = std::move(*opened);
  EXPECT_EQ(*store->checkpoint(), 0);
  EXPECT_FALSE(*store->get(key));
  ASSERT_TRUE(store->apply({{key, "state"}}, 7));
  EXPECT_EQ(store->get(key).value().value_or("<absent>"), "state");
  EXPECT_EQ(*store->checkpoint(), 7);
  store.reset();
  opened = StateStore::open(directory);
  ASSERT_TRUE(opened);
  store = std::move(*opened);
  EXPECT_EQ(store->get(key).value().value_or("<absent>"), "state");
  EXPECT_EQ(*store->checkpoint(), 7);
  ASSERT_TRUE(store->apply({{key, std::nullopt}}, 10));
  EXPECT_TRUE(store->entries()->empty());
  EXPECT_EQ(*store->checkpoint(), 10);
  ASSERT_TRUE(store->apply({{key, "recovered"}}, 12));
  ASSERT_TRUE(store->clear());
  EXPECT_TRUE(store->entries()->empty());
  EXPECT_EQ(*store->checkpoint(), 0);
  store.reset();
  std::filesystem::remove_all(directory);
}

TEST(CorrelationStateStore, SeparateRuleFamiliesAndPartitionsDoNotShareState) {
  const auto directory = "/tmp/psim-correlation-test-" + p::stream::uuid7();
  auto first = StateStore::open(directory + "/0");
  auto second = StateStore::open(directory + "/1");
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  auto spec = test::rule();
  const auto a = state_key(test::kTenant, spec, "key");
  spec.set_rule_id(test::kSite);
  const auto b = state_key(test::kTenant, spec, "key");
  ASSERT_TRUE((*first)->apply({{a, "a"}, {b, "b"}}, 20));
  EXPECT_EQ((*first)->entries()->size(), 2);
  EXPECT_TRUE((*second)->entries()->empty());
  EXPECT_EQ(*(*second)->checkpoint(), 0);
  EXPECT_FALSE(StateStore::open(directory + "/0"));
  first->reset();
  second->reset();
  std::filesystem::remove_all(directory);
}
}  // namespace
}  // namespace psim::correlation::live
