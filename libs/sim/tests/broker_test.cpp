#include "psim/sim/broker.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <tuple>

namespace {

using psim::sim::Broker;
using psim::sim::Message;

TEST(SimBroker, KeepsUncommittedOutputInvisibleAndCommitsAtomically) {
  Broker broker;
  broker.create_topic("in", 1);
  broker.create_topic("out", 1);
  const Message message{.tenant = "t", .id = "1", .payload = "payload"};
  broker.append("in", 0, message);
  auto transaction = broker.begin("producer");
  transaction.publications.push_back({.topic = "out", .partition = 0, .message = message});
  transaction.offsets.push_back({.group = "g", .topic = "in", .partition = 0, .next = 1});
  EXPECT_FALSE(broker.read("out", 0, 0));
  EXPECT_EQ(broker.offset("g", "in", 0), 0U);
  EXPECT_TRUE(broker.commit(transaction));
  EXPECT_EQ(broker.read("out", 0, 0), message);
  EXPECT_EQ(broker.offset("g", "in", 0), 1U);
  EXPECT_FALSE(broker.commit(transaction));
  EXPECT_EQ(broker.size("out", 0), 1U);
}

TEST(SimBroker, FencesOldProducerAndPreservesDurableRecordsAfterRestart) {
  Broker broker;
  broker.create_topic("topic", 1);
  auto stale = broker.begin("p");
  stale.publications.push_back(
      {.topic = "topic", .partition = 0, .message = {.tenant = "t", .id = "1", .payload = "v"}});
  auto current = broker.begin("p");
  EXPECT_FALSE(broker.commit(stale));
  EXPECT_EQ(broker.size("topic", 0), 0U);
  current.publications = stale.publications;
  ASSERT_TRUE(broker.commit(current));
  std::ignore = broker.begin("p");
  EXPECT_EQ(broker.size("topic", 0), 1U);
}

TEST(SimBroker, InvalidOffsetsNeverPartiallyPublish) {
  Broker broker;
  broker.create_topic("topic", 2);
  broker.append("topic", 0, {.tenant = "t", .id = "1", .payload = "v"});
  auto transaction = broker.begin("p");
  transaction.publications.push_back(
      {.topic = "topic", .partition = 1, .message = {.tenant = "t", .id = "2", .payload = "v"}});
  transaction.offsets.push_back({.group = "g", .topic = "topic", .partition = 0, .next = 2});
  EXPECT_FALSE(broker.commit(transaction));
  EXPECT_EQ(broker.size("topic", 1), 0U);
  transaction.offsets.front().next = 1;
  ASSERT_TRUE(broker.commit(transaction));
  auto regressing = broker.begin("p");
  regressing.offsets.push_back({.group = "g", .topic = "topic", .partition = 0, .next = 0});
  EXPECT_FALSE(broker.commit(regressing));
  EXPECT_EQ(broker.offset("g", "topic", 0), 1U);
}

TEST(SimBroker, RejectsInvalidTopologyAndStagesBeforeThrowing) {
  Broker broker;
  EXPECT_THROW(broker.create_topic("empty", 0), std::invalid_argument);
  broker.create_topic("topic", 1);
  EXPECT_THROW(broker.create_topic("topic", 1), std::invalid_argument);
  auto transaction = broker.begin("p");
  transaction.publications.push_back(
      {.topic = "topic", .partition = 0, .message = {.tenant = "t", .id = "1", .payload = "v"}});
  transaction.publications.push_back(
      {.topic = "unknown", .partition = 0, .message = {.tenant = "t", .id = "2", .payload = "v"}});
  EXPECT_THROW(std::ignore = broker.commit(transaction), std::out_of_range);
  EXPECT_EQ(broker.size("topic", 0), 0U);
  auto foreign = transaction;
  foreign.producer = "unknown";
  EXPECT_FALSE(broker.commit(foreign));
}

}  // namespace
