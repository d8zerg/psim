#include "monitor.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include "common.hpp"

namespace psim::monitoring {
namespace {
TEST(Monitoring, MissingBackupStatusNeverInventsSuccess) {
  const auto path = fs::temp_directory_path() / ("psim-monitoring-" + std::to_string(seconds()) + ".json");
  EXPECT_FALSE(fs::exists(path));
  const auto missing = backup_metrics(path, Json::array({"plans"}));
  EXPECT_NE(missing.find("psim_backup_status_valid 0"), std::string::npos);
  EXPECT_NE(missing.find("psim_backup_last_success_seconds{backup=\"plans\"} 0"), std::string::npos);
  const auto now = seconds();
  Json document{{"updated_at_seconds", now},
                {"backups", {{"plans", {{"last_success_seconds", now - 100}, {"size_bytes", 1024}}}}}};
  const auto save = [&path, &document] {
    std::ofstream output(path);
    output << document;
  };
  save();
  EXPECT_NE(backup_metrics(path, Json::array({"plans"})).find("psim_backup_status_valid 1"), std::string::npos);
  for (const double value : {-1.0, now + 1000}) {
    document["backups"]["plans"]["last_success_seconds"] = value;
    save();
    EXPECT_NE(backup_metrics(path, Json::array({"plans"})).find("psim_backup_status_valid 0"), std::string::npos);
  }
  fs::remove(path);
}

TEST(Monitoring, MetricsEscapeLabelsAndRejectNonFiniteValues) {
  EXPECT_THROW(sample("value", std::numeric_limits<double>::infinity()), std::runtime_error);
  EXPECT_NE(sample("value", 1, {{"name", "line\nquote\""}}).find("name=\"line\\nquote\\\"\""), std::string::npos);
}

}  // namespace
}  // namespace psim::monitoring
