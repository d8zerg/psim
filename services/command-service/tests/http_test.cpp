#include "http.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"
#include "repository.hpp"

namespace psim::command_service {
TEST(CommandHttp, RejectsInvalidRequestsBeforeStorage) {
  asio::io_context io;
  Api api(nullptr);
  for (const auto* target : {"/v1/commands?unknown=x", "/v1/commands?pageSize=1x", "/v1/commands?pageSize=1&pageSize=2",
                             "/v1/commands?states=%ZZ", "/unknown"}) {
    Request request(http::verb::get, target, 11);
    auto pending = asio::co_spawn(io, api.handle(std::move(request), {}), asio::use_future);
    io.run();
    io.restart();
    EXPECT_FALSE(pending.get());
  }
  Request request(http::verb::post, "/v1/commands/018f0000-0000-7000-8000-000000000001:approve", 11);
  request.set(http::field::content_type, "application/json");
  request.body() = "{}";
  auto pending = asio::co_spawn(io, api.handle(std::move(request), {}), asio::use_future);
  io.run();
  ASSERT_FALSE(pending.get());
}

TEST(CommandHttp, OversizeCommandFailsBeforeStorage) {
  asio::io_context io;
  Api api(nullptr);
  Json body = {{"commandId", "018f0000-0000-7000-8000-000000000001"},
               {"deviceId", "018f0000-0000-7000-8000-000000000002"},
               {"capability", "arm"},
               {"parameters", Json::object()}};
  for (int index = 0; index < 32; ++index) {
    body["parameters"]["p" + std::to_string(index)] = std::string(1024, 'x');
  }
  Request request(http::verb::post, "/v1/commands", 11);
  request.set(http::field::content_type, "application/json");
  request.body() = body.dump();
  auto pending = asio::co_spawn(io, api.handle(std::move(request), {}), asio::use_future);
  io.run();
  const auto result = pending.get();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code(), p::ErrorCode::kCommonInvalidArgument);
}

TEST(CommandHttp, ProblemUsesCatalog) {
  const auto response = Api::problem(p::Error(p::ErrorCode::kCommandApprovalSelf, "private detail"));
  EXPECT_EQ(response.result_int(), 403);
  EXPECT_EQ(response.body().find("private detail"), std::string::npos);
}
}  // namespace psim::command_service
