#include <gtest/gtest.h>

#include "http.hpp"
#include "psim/platform/error_codes.hpp"

TEST(IncidentApi, ErrorResponsesUseStableCatalogCodes) {
  const auto response = psim::incident_service::Api::problem(
      psim::platform::Error(psim::platform::ErrorCode::kIncidentMandatoryStepsPending, "private detail"));
  EXPECT_EQ(response.result_int(), 409);
  EXPECT_NE(response.body().find("INCIDENT_MANDATORY_STEPS_PENDING"), std::string::npos);
  EXPECT_EQ(response.body().find("private detail"), std::string::npos);
}
