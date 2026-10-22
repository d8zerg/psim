#include "model.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <google/protobuf/util/json_util.h>
#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

#include "escalation.hpp"
#include "http.hpp"
#include "psim/common/v1/types.pb.h"
#include "psim/response/v1/plan.pb.h"

namespace psim::response_engine {
namespace {
response::v1::ResponsePlan plan() {
  response::v1::ResponsePlan value;
  const auto status = google::protobuf::util::JsonStringToMessage(R"({"planId":"018f0000-0000-7000-8000-000000000001",
    "name":"Response","startStepId":"confirm","steps":[
    {"stepId":"confirm","title":"Confirm","mandatory":true,"nextStepId":"command","instruction":{"text":"Read"}},
    {"stepId":"command","title":"Arm","nextStepId":"notify","command":{"deviceId":"018f0000-0000-7000-8000-000000000002","capability":"arm","automatic":true}},
    {"stepId":"notify","title":"Notify","nextStepId":"wait","notify":{"recipients":[{"role":"supervisor"}],"channels":["NOTIFICATION_CHANNEL_EMAIL"]}},
    {"stepId":"wait","title":"Wait","wait":{"duration":"1s"}}]})",
                                                                  &value);
  EXPECT_TRUE(status.ok());
  return value;
}

TEST(ResponseModel, ReplayUsesSimulatedEffects) {
  const auto value = plan();
  auto result = replay(value, Json::array({{{"stepId", "confirm"}}}));
  EXPECT_EQ(result["state"], 2);
  EXPECT_EQ(result["externalCalls"], 0);
  ASSERT_EQ(result["effects"].size(), 3);
  for (const auto& effect : result["effects"]) {
    EXPECT_TRUE(effect["simulated"].get<bool>());
  }
  EXPECT_EQ(replay(value, Json::array())["state"], 1);
  EXPECT_THROW((void)replay(value, Json::array({{{"stepId", "other"}}})), std::invalid_argument);
}

TEST(ResponseModel, BindingSpecificityAndOverlap) {
  response::v1::PlanBinding left;
  response::v1::PlanBinding right;
  left.set_incident_type_id("type");
  right = left;
  EXPECT_TRUE(overlaps(left, right));
  left.add_zone_ids("zone");
  EXPECT_GT(specificity(left), specificity(right));
  EXPECT_FALSE(overlaps(left, right));
  right.add_zone_ids("other");
  EXPECT_FALSE(overlaps(left, right));
  right.add_zone_ids("zone");
  EXPECT_TRUE(overlaps(left, right));
  EXPECT_TRUE(matches(left, "type", "site", "zone", 1));
  EXPECT_FALSE(matches(left, "type", "site", "other", 1));
  left.add_priorities(common::v1::PRIORITY_P1);
  EXPECT_FALSE(matches(left, "type", "site", "zone", 2));
}

TEST(ResponseModel, InvalidParametersAreRejectedBeforeStorage) {
  auto value = plan();
  ASSERT_TRUE(model(value));
  value.mutable_steps(1)->mutable_command()->set_target_expression("arbitrary expression");
  EXPECT_FALSE(model(value));
  value = plan();
  value.mutable_steps(3)->mutable_wait()->mutable_duration()->set_seconds(-1);
  EXPECT_FALSE(model(value));
  value = plan();
  value.mutable_steps(2)->mutable_notify()->clear_recipients();
  EXPECT_FALSE(model(value));
  value = plan();
  value.mutable_steps(0)->set_next_step_id("missing");
  EXPECT_FALSE(model(value));
}

TEST(ResponseHttp, RejectsUnknownFieldsPathsAndPreconditions) {
  boost::asio::io_context io;
  Api api(nullptr);
  for (const auto* path : {"/unknown", "/v1/plans?unknown=x", "/v1/plans?pageSize=1x",
                           "/v1/plans?pageSize=1&pageSize=2", "/v1/plans?includeArchived=maybe"}) {
    Request request(http::verb::get, path, 11);
    auto future = boost::asio::co_spawn(io, api.handle(std::move(request), {}), boost::asio::use_future);
    io.run();
    io.restart();
    EXPECT_FALSE(future.get());
  }
  Request request(http::verb::post, "/v1/runs/018f0000-0000-7000-8000-000000000001/steps/confirm:complete", 11);
  request.set(http::field::content_type, "application/json");
  request.body() = "{}";
  auto future = boost::asio::co_spawn(io, api.handle(std::move(request), {}), boost::asio::use_future);
  io.run();
  ASSERT_FALSE(future.get());
}
}  // namespace
}  // namespace psim::response_engine

TEST(ResponseEscalation, RejectsAmbiguousPolicies) {
  using namespace psim::response_engine;
  psim::response::v1::EscalationPolicy policy;
  policy.set_policy_id("018f0000-0000-7000-8000-000000000001");
  policy.set_name("Site SLA");
  auto* sla = policy.add_sla();
  sla->set_priority(psim::common::v1::PRIORITY_P2);
  sla->mutable_acknowledge_within()->set_seconds(60);
  sla->mutable_resolve_within()->set_seconds(300);
  auto* level = policy.add_levels();
  level->set_level(1);
  level->add_recipients()->set_current_duty(true);
  level->add_channels(psim::response::v1::NOTIFICATION_CHANNEL_IN_APP);
  ASSERT_TRUE(validate_policy(policy));
  auto duplicate = policy;
  *duplicate.add_sla() = policy.sla(0);
  EXPECT_FALSE(validate_policy(duplicate));
  duplicate = policy;
  *duplicate.add_levels() = policy.levels(0);
  duplicate.mutable_levels(1)->set_level(2);
  EXPECT_FALSE(validate_policy(duplicate));
  duplicate.mutable_levels(1)->mutable_delay()->set_seconds(10);
  EXPECT_TRUE(validate_policy(duplicate));
  duplicate.mutable_levels(1)->mutable_recipients(0)->set_current_duty(false);
  EXPECT_FALSE(validate_policy(duplicate));
  duplicate = policy;
  duplicate.mutable_sla(0)->mutable_acknowledge_within()->set_nanos(-1);
  EXPECT_FALSE(validate_policy(duplicate));
}
