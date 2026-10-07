#include "psim/platform/error.hpp"

#include <gtest/gtest.h>

#include <string>

#include "psim/platform/error_codes.hpp"

namespace {

using psim::platform::Error;
using psim::platform::ErrorCode;
using psim::platform::fail;
using psim::platform::info;
using psim::platform::parse_error_code;
using psim::platform::Result;

TEST(ErrorCodes, CarryTheCatalogMetadata) {
  const auto& row = info(ErrorCode::kCommonNotFound);
  EXPECT_EQ(row.name, "COMMON_NOT_FOUND");
  EXPECT_EQ(row.http_status, 404);
  EXPECT_EQ(row.grpc_status, "NOT_FOUND");
  EXPECT_FALSE(row.retryable);
}

TEST(ErrorCodes, ParseByName) {
  EXPECT_EQ(parse_error_code("COMMON_INVALID_ARGUMENT"), ErrorCode::kCommonInvalidArgument);
  EXPECT_FALSE(parse_error_code("NO_SUCH_CODE").has_value());
}

Result<int> parse_port(const std::string& text) {
  if (text.empty()) {
    return fail(ErrorCode::kCommonInvalidArgument, "port is empty");
  }
  return std::stoi(text);
}

TEST(Error, FlowsThroughResultWithContext) {
  EXPECT_EQ(parse_port("8080").value(), 8080);

  const auto result = parse_port("");
  ASSERT_FALSE(result.has_value());
  const Error error = Error(result.error()).context("admin.listen");
  EXPECT_EQ(error.code(), ErrorCode::kCommonInvalidArgument);
  EXPECT_EQ(error.name(), "COMMON_INVALID_ARGUMENT");
  EXPECT_EQ(error.message(), "admin.listen: port is empty");
  EXPECT_FALSE(error.retryable());
}

}  // namespace
