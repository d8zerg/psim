#include "psim/platform/config.hpp"

#include <gtest/gtest.h>
#include <stdlib.h>  // NOLINT(modernize-deprecated-headers): setenv, mkdtemp are POSIX

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using psim::platform::config::Config;
using psim::platform::config::load;
using psim::platform::config::plan_reload;
using psim::platform::config::Problem;
using psim::platform::config::Problems;
using psim::platform::config::process_environment;
using psim::platform::config::Schema;
using psim::platform::config::Sources;

constexpr std::string_view kSchema = R"({
  "type": "object",
  "additionalProperties": false,
  "required": ["service"],
  "properties": {
    "service": {
      "type": "object", "additionalProperties": false, "required": ["instance"],
      "properties": {"instance": {"type": "string", "minLength": 1}}
    },
    "log": {
      "type": "object", "additionalProperties": false,
      "properties": {"level": {"type": "string", "enum": ["debug", "info", "warn"], "default": "info",
                               "x-psim-reload": true}}
    },
    "admin": {
      "type": "object", "additionalProperties": false,
      "properties": {"port": {"type": "integer", "minimum": 1, "maximum": 65535, "default": 9100},
                     "enabled": {"type": "boolean", "default": true},
                     "ratio": {"type": "number", "default": 0.01}}
    },
    "database": {
      "type": "object", "additionalProperties": false,
      "properties": {"password_file": {"type": "string", "x-psim-secret": true}}
    }
  }
})";

class TempDir {
 public:
  TempDir() {
    std::string pattern = (fs::temp_directory_path() / "psim-config-XXXXXX").string();
    if (::mkdtemp(pattern.data()) == nullptr) {
      throw std::runtime_error("mkdtemp failed");
    }
    path_ = pattern;
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  ~TempDir() { fs::remove_all(path_); }

  fs::path write(const std::string& name, const std::string& text, fs::perms perms = fs::perms::owner_read) const {
    const auto file = path_ / name;
    std::ofstream(file) << text;
    fs::permissions(file, perms);
    return file;
  }

 private:
  fs::path path_;
};

Schema schema() {
  return Schema::parse(kSchema).value();
}

bool has_problem(const Problems& problems, std::string_view path, std::string_view text) {
  return std::ranges::any_of(
      problems, [&](const Problem& p) { return p.path == path && p.message.find(text) != std::string::npos; });
}

TEST(Config, LoadsTheFileAndAppliesDefaultsInsideMissingSections) {
  const TempDir dir;
  const auto file = dir.write("svc.yaml", "service:\n  instance: normalizer-0\nadmin:\n  port: 9200\n");

  const auto config = load(schema(), Sources{.file = file, .environment = {}});

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->get<std::string>("service.instance"), "normalizer-0");
  EXPECT_EQ(config->get<std::int64_t>("admin.port"), 9200);
  EXPECT_TRUE(config->get<bool>("admin.enabled"));
  EXPECT_EQ(config->get<std::string>("log.level"), "info");
  EXPECT_FALSE(config->has("database.password_file"));
}

TEST(Config, EnvironmentOverridesTheFileWithTypesFromTheSchema) {
  const TempDir dir;
  const auto file = dir.write("svc.yaml", "service:\n  instance: a\n");
  const std::map<std::string, std::string> env{{"PSIM__ADMIN__PORT", "9300"}, {"PSIM__ADMIN__ENABLED", "false"},
                                               {"PSIM__ADMIN__RATIO", "0.5"}, {"PSIM__SERVICE__INSTANCE", "b"},
                                               {"PSIM_PLATFORM", "debian12"}, {"HOME", "/root"}};

  const auto config = load(schema(), Sources{.file = file, .environment = env});

  ASSERT_TRUE(config.has_value()) << config.error().front().message;
  EXPECT_EQ(config->get<std::int64_t>("admin.port"), 9300);
  EXPECT_FALSE(config->get<bool>("admin.enabled"));
  EXPECT_DOUBLE_EQ(config->get<double>("admin.ratio"), 0.5);
  EXPECT_EQ(config->get<std::string>("service.instance"), "b");
}

TEST(Config, ReportsEveryProblemAtOnce) {
  const TempDir dir;
  const auto file = dir.write("svc.yaml", "admin:\n  port: 70000\nlog:\n  level: chatty\nunknown: 1\n");
  const std::map<std::string, std::string> env{{"PSIM__ADMIN__ENABLED", "maybe"}, {"PSIM__NO__SUCH", "1"}};

  const auto config = load(schema(), Sources{.file = file, .environment = env});

  ASSERT_FALSE(config.has_value());
  const auto& problems = config.error();
  EXPECT_TRUE(has_problem(problems, "admin.port", "maximum")) << problems.size();
  EXPECT_TRUE(has_problem(problems, "log.level", "enum"));
  EXPECT_TRUE(has_problem(problems, "admin.enabled", "expected boolean"));
  EXPECT_TRUE(has_problem(problems, "no.such", "no such setting"));
  EXPECT_TRUE(has_problem(problems, "service", "required"));  // the required section is missing
  EXPECT_GE(problems.size(), 5U);
}

TEST(Config, ReadsSecretsFromProtectedFilesOnly) {
  const TempDir dir;
  const auto secret = dir.write("db-password", "s3cr3t\n", fs::perms::owner_read);
  const auto file =
      dir.write("svc.yaml", "service: {instance: a}\ndatabase:\n  password_file: " + secret.string() + "\n");

  const auto config = load(schema(), Sources{.file = file, .environment = {}});

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->secret("database.password_file"), "s3cr3t");
  EXPECT_EQ(config->document().dump().find("s3cr3t"), std::string::npos);
}

TEST(Config, RejectsSecretsReadableByOthersOrGivenThroughTheEnvironment) {
  const TempDir dir;
  const auto secret = dir.write("db-password", "x", fs::perms::owner_read | fs::perms::group_read);
  const auto file =
      dir.write("svc.yaml", "service: {instance: a}\ndatabase:\n  password_file: " + secret.string() + "\n");

  const auto open_file = load(schema(), Sources{.file = file, .environment = {}});
  ASSERT_FALSE(open_file.has_value());
  EXPECT_TRUE(has_problem(open_file.error(), "database.password_file", "mode 0400"));

  const auto from_env = load(schema(), Sources{.file = file, .environment = {{"PSIM__DATABASE__PASSWORD_FILE", "/x"}}});
  ASSERT_FALSE(from_env.has_value());
  EXPECT_TRUE(has_problem(from_env.error(), "database.password_file", "files only"));

  const auto missing = dir.write("svc2.yaml", "service: {instance: a}\ndatabase: {password_file: /nonexistent}\n");
  EXPECT_TRUE(has_problem(load(schema(), Sources{.file = missing, .environment = {}}).error(), "database.password_file",
                          "not readable"));
}

TEST(Config, ReportsUnreadableFilesAndBrokenSchemas) {
  const auto missing = load(schema(), Sources{.file = "/nonexistent/svc.yaml", .environment = {}});
  ASSERT_FALSE(missing.has_value());
  EXPECT_TRUE(has_problem(missing.error(), "", "cannot read"));

  const auto broken = Schema::parse("{not json");
  ASSERT_FALSE(broken.has_value());
  EXPECT_TRUE(has_problem(broken.error(), "", "invalid configuration schema"));
}

TEST(Config, KeepsQuotedScalarsAsStrings) {
  const TempDir dir;
  const auto file = dir.write("svc.yaml", "service:\n  instance: \"42\"\n");
  const auto config = load(schema(), Sources{.file = file, .environment = {}});
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->get<std::string>("service.instance"), "42");
}

TEST(Reload, SeparatesRunTimeSettingsFromRestartOnes) {
  const auto s = schema();
  const Config before(
      nlohmann::json::parse(R"({"service":{"instance":"a"},"log":{"level":"info"},"admin":{"port":1}})"), {});
  const Config after(nlohmann::json::parse(
                         R"({"service":{"instance":"a"},"log":{"level":"debug"},"admin":{"port":2,"enabled":true}})"),
                     {});

  const auto plan = plan_reload(s, before, after);

  EXPECT_EQ(plan.apply, std::vector<std::string>{"log.level"});
  EXPECT_EQ(plan.restart_required, (std::vector<std::string>{"admin.port", "admin.enabled"}));
  EXPECT_TRUE(s.reloadable("log.level"));
  EXPECT_FALSE(s.reloadable("admin"));
}

TEST(Environment, IsReadFromTheProcess) {
  ::setenv("PSIM__TEST__PROBE", "1", 1);  // NOLINT(concurrency-mt-unsafe): single-threaded test
  EXPECT_EQ(process_environment().at("PSIM__TEST__PROBE"), "1");
}

}  // namespace
