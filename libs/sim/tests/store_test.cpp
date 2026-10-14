#include "psim/sim/store.hpp"

#include <gtest/gtest.h>

#include <utility>

namespace {

using psim::sim::Store;

TEST(SimStore, AbandonedTransactionHasNoEffectsAndOutboxCommitIsAtomic) {
  Store store;
  auto abandoned = store.begin();
  EXPECT_TRUE(Store::apply(abandoned, {.tenant = "a", .id = "1", .payload = "v"}));
  EXPECT_TRUE(store.effects().empty());
  EXPECT_FALSE(store.pending());
  ASSERT_TRUE(store.commit(std::move(abandoned)));
  EXPECT_EQ(store.effects().size(), 1U);
  ASSERT_EQ(store.pending(), 0U);
  EXPECT_EQ(store.entry(0).message.payload, "v");
}

TEST(SimStore, DedupesPerTenantAndAllowsTheSameIdInAnotherTenant) {
  Store store;
  auto transaction = store.begin();
  ASSERT_TRUE(Store::apply(transaction, {.tenant = "a", .id = "1", .payload = "first"}));
  EXPECT_FALSE(Store::apply(transaction, {.tenant = "a", .id = "1", .payload = "ignored duplicate"}));
  EXPECT_TRUE(Store::apply(transaction, {.tenant = "b", .id = "1", .payload = "second"}));
  ASSERT_TRUE(store.commit(std::move(transaction)));
  EXPECT_EQ(store.effects().size(), 2U);
  EXPECT_EQ(store.effects().at({"a", "1"}).payload, "first");
  store.acknowledge(0);
  EXPECT_EQ(store.pending(), 1U);
  store.acknowledge(1);
  EXPECT_FALSE(store.pending());
}

TEST(SimStore, FencesConflictingSnapshotsAndOutboxAcknowledgements) {
  Store store;
  auto first = store.begin();
  auto conflicting = store.begin();
  ASSERT_TRUE(Store::apply(first, {.tenant = "a", .id = "1", .payload = "v"}));
  ASSERT_TRUE(store.commit(std::move(first)));
  EXPECT_FALSE(store.commit(std::move(conflicting)));
  auto stale = store.begin();
  store.acknowledge(0);
  EXPECT_FALSE(store.commit(std::move(stale)));
  EXPECT_FALSE(store.pending());
}

TEST(SimStore, SinkCanCommitWithoutPublishingAnOutboxRecord) {
  Store store;
  auto transaction = store.begin();
  ASSERT_TRUE(Store::apply(transaction, {.tenant = "a", .id = "1", .payload = "v"}, false));
  ASSERT_TRUE(store.commit(std::move(transaction)));
  EXPECT_EQ(store.effects().size(), 1U);
  EXPECT_FALSE(store.pending());
}

}  // namespace
