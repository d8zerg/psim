#include "configuration.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "model.hpp"

namespace psim::resource_catalog {
TEST(CatalogConfiguration, RejectsUnsupportedMappingWithoutAcceptingArbitraryExpressions) {
  const Json mapping{
      {"connectorType", "test"},
      {"taxonomyVersion", "1"},
      {"rules",
       Json::array(
           {{{"rawCode", "A*"}, {"condition", "raw.raw_code.startsWith(\"A\")"}, {"type", "intrusion.zone.alarm"}}})}};
  const std::set<std::string> taxonomy{"intrusion.zone.alarm"};
  EXPECT_TRUE(configuration("mapping", "test", mapping, taxonomy));
  EXPECT_FALSE(configuration("mapping", "other", mapping, taxonomy));
  auto bad = mapping;
  bad["rules"][0]["rawCode"] = "A*B";
  EXPECT_FALSE(configuration("mapping", "test", bad, taxonomy));
  bad = mapping;
  bad["rules"][0]["condition"] = "raw.run()";
  EXPECT_FALSE(configuration("mapping", "test", bad, taxonomy));
  EXPECT_FALSE(configuration("mapping", "test", mapping, {}));
}

TEST(CatalogConfiguration, RetentionAndProcessingRequirePositiveBoundedDurations) {
  auto retention = default_configuration("retention");
  EXPECT_TRUE(configuration("retention", "retention", retention, {}));
  retention["audit"] = "-1s";
  EXPECT_FALSE(configuration("retention", "retention", retention, {}));
  retention = default_configuration("retention");
  retention["eventHistoryAll"] = "99999999s";
  EXPECT_FALSE(configuration("retention", "retention", retention, {}));
  auto processing = default_configuration("processing");
  EXPECT_TRUE(configuration("processing", "processing", processing, {}));
  processing["significanceThreshold"] = "SEVERITY_UNSPECIFIED";
  EXPECT_FALSE(configuration("processing", "processing", processing, {}));
  processing = default_configuration("processing");
  processing.erase("sourceIdleTimeout");
  EXPECT_FALSE(configuration("processing", "processing", processing, {}));
}
}  // namespace psim::resource_catalog
