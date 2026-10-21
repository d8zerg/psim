#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <clickhouse/columns/array.h>
#include <clickhouse/columns/date.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>
#include <utility>

#include "facts.hpp"
#include "http.hpp"
#include "psim/config/v1/config.pb.h"
#include "psim/platform/error_codes.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/processing/v1/signal.pb.h"
#include "search.hpp"

namespace psim::event_history {
namespace {
constexpr auto kTenant = "11111111-1111-4111-8111-111111111111";
constexpr auto kEvent = "22222222-2222-4222-8222-222222222222";

processing::v1::SignalRecord signal() {
  processing::v1::SignalRecord r;
  auto* e = r.mutable_envelope();
  e->set_tenant_id(kTenant);
  e->set_message_id(kEvent);
  e->mutable_occurred_at()->set_seconds(1000);
  auto* s = r.mutable_signal();
  s->set_site_id(kTenant);
  s->mutable_origin()->set_kind(processing::v1::SIGNAL_ORIGIN_KIND_RULE);
  s->mutable_origin()->set_rule_id(kEvent);
  s->mutable_origin()->set_rule_version(2);
  s->add_event_ids(kEvent);
  return r;
}

p::security::Principal principal() {
  return {.subject = "alice",
          .tenant = kTenant,
          .roles = {"operator"},
          .scopes = {std::string("zone:") + kEvent},
          .expires_at = std::chrono::system_clock::now() + std::chrono::hours(1)};
}

Json interval() {
  return {{"from", "1970-01-01T00:00:01Z"}, {"to", "1970-01-02T00:00:01Z"}};
}

TEST(HistoryFacts, DeduplicatesEvidenceAndRetainsTimerDrivenSignals) {
  Facts const facts;
  auto r = signal();
  r.mutable_signal()->add_event_ids(kEvent);
  EXPECT_TRUE(valid_signal(r));
  auto timer = signal();
  timer.mutable_signal()->clear_event_ids();
  timer.mutable_signal()->mutable_absence_deadline()->set_seconds(1000);
  const auto converted = facts.signals(std::array{r, timer, processing::v1::SignalRecord{}});
  ASSERT_TRUE(converted);
  EXPECT_EQ(converted->block.GetRowCount(), 2);
  EXPECT_EQ(converted->rejected.size(), 1);
  EXPECT_EQ(converted->block[9]->AsStrict<::clickhouse::ColumnArray>()->GetAsColumn(0)->Size(), 1);
  EXPECT_EQ(converted->block[9]->AsStrict<::clickhouse::ColumnArray>()->GetAsColumn(1)->Size(), 0);
  EXPECT_EQ(converted->block[11]->AsStrict<::clickhouse::ColumnDateTime64>()->At(0), 7777000000LL);
}

TEST(HistoryFacts, RejectsInvalidEnvelopeResourcesEvidenceOriginAndTimes) {
  for (int fault = 0; fault < 10; ++fault) {
    auto r = signal();
    auto* s = r.mutable_signal();
    switch (fault) {
      case 0:
        r.mutable_envelope()->set_tenant_id("other");
        break;
      case 1:
        r.mutable_envelope()->set_message_id("invalid");
        break;
      case 2:
        r.mutable_envelope()->mutable_occurred_at()->set_seconds(-1);
        break;
      case 3:
        s->set_site_id("invalid");
        break;
      case 4:
        s->set_zone_id("invalid");
        break;
      case 5:
        s->add_event_ids("invalid");
        break;
      case 6:
        s->clear_event_ids();
        break;
      case 7:
        s->mutable_origin()->set_rule_version(0);
        break;
      case 8:
        s->mutable_first_event_at()->set_seconds(1001);
        s->mutable_last_event_at()->set_seconds(1000);
        break;
      default:
        s->mutable_first_event_at()->set_nanos(-1);
        break;
    }
    EXPECT_FALSE(valid_signal(r)) << fault;
  }
}

TEST(HistoryFacts, RetentionIsVersionedAndBounded) {
  Facts facts;
  config::v1::ConfigRecord config;
  EXPECT_TRUE(facts.config(config));
  config.mutable_envelope()->set_tenant_id(kTenant);
  config.mutable_retention()->mutable_event_history_all()->set_seconds(30);
  config.mutable_retention()->set_version(2);
  config.mutable_retention()->mutable_event_history_significant()->set_seconds(60);
  ASSERT_TRUE(facts.config(config));
  EXPECT_EQ(facts.retention(kTenant), 60);
  config.mutable_retention()->set_version(1);
  config.mutable_retention()->mutable_event_history_significant()->set_seconds(30);
  EXPECT_TRUE(facts.config(config));
  EXPECT_EQ(facts.retention(kTenant), 60);
  config.mutable_retention()->mutable_event_history_significant()->set_nanos(1);
  EXPECT_FALSE(facts.config(config));
  EXPECT_EQ(facts_migration("psim").expand.size(), 5);
  EXPECT_TRUE(facts_migration("drop;").expand.empty());
}

TEST(HistorySearch, RequiresBoundedIntervalAndKeepsFiltersOutOfSql) {
  const auto user = principal();
  auto input = interval();
  input["types"] = Json::array({"' OR 1=1 --"});
  auto query = search_query(user, "search", "", input);
  ASSERT_TRUE(query);
  EXPECT_EQ(query->tenant, kTenant);
  EXPECT_EQ(query->statement.find("' OR 1=1"), std::string::npos);
  EXPECT_NE(query->parameters.at("types").find("' OR 1=1"), std::string::npos);
  EXPECT_NE(query->parameters.at("zones").find(kEvent), std::string::npos);
  EXPECT_EQ(query->parameters.at("all"), "0");
  EXPECT_FALSE(search_query(user, "search", "", Json::object()));
  input = interval();
  input["to"] = input["from"];
  EXPECT_FALSE(search_query(user, "search", "", input));
  input = interval();
  input["to"] = "9999-12-31T00:00:00Z";
  EXPECT_FALSE(search_query(user, "search", "", input));
  input = interval();
  input["pageSize"] = 1001;
  EXPECT_FALSE(search_query(user, "search", "", input));
  input = interval();
  input["pageToken"] = "bad";
  EXPECT_FALSE(search_query(user, "search", "", input));
  input = interval();
  input["siteIds"] = Json::array({"invalid"});
  EXPECT_FALSE(search_query(user, "search", "", input));
  EXPECT_FALSE(search_query(user, "get", "bad", Json::object()));
}

TEST(HistoryApi, RejectsMalformedOrUnsupportedRequestsBeforeStorage) {
  boost::asio::io_context loop;
  auto scenario = []() -> boost::asio::awaitable<void> {
    Search search(nullptr, {});
    Api api(&search);
    for (const auto* target : {"/v1/events?pageSize=1x", "/v1/events?unknown=x", "/v1/events?pageSize=1&pageSize=2",
                               "/v1/events?types=%XX", "/v1/other"}) {
      Request request(http::verb::get, target, 11);
      auto result = co_await api.handle(std::move(request), principal());
      EXPECT_FALSE(result);
    }
    Request request(http::verb::post, "/v1/events", 11);
    EXPECT_FALSE(co_await api.handle(std::move(request), principal()));
    auto user = principal();
    user.roles = {"admin"};
    EXPECT_FALSE(co_await search.read(std::move(user), "get", kEvent, {}));
    const auto response = Api::problem(p::Error(p::ErrorCode::kAuthForbidden, "private detail"));
    EXPECT_EQ(response.result_int(), 403);
    EXPECT_EQ(response.body().find("private detail"), std::string::npos);
  };
  auto future = boost::asio::co_spawn(loop, scenario(), boost::asio::use_future);
  loop.run();
  future.get();
}
}  // namespace
}  // namespace psim::event_history
