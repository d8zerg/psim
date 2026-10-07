#pragma once

#include <nlohmann/json.hpp>

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace psim::platform::config {

/// A problem of a configuration; loading reports all of them at once.
struct Problem {
  std::string path;  // dotted path, for example "admin.listen"; empty for the whole document
  std::string message;

  friend bool operator==(const Problem&, const Problem&) = default;
};

using Problems = std::vector<Problem>;

/// JSON Schema (draft 7) of a service configuration with PSIM annotations on properties:
///   "x-psim-secret": true  - the value is the path of a file holding the secret (SR-09);
///   "x-psim-reload": true  - the setting applies at run time without a restart.
class Schema {
 public:
  [[nodiscard]] static std::expected<Schema, Problems> parse(std::string_view json_text);

  [[nodiscard]] const nlohmann::json& document() const noexcept { return document_; }

  [[nodiscard]] const std::set<std::string>& secret_paths() const noexcept { return secrets_; }

  [[nodiscard]] bool reloadable(std::string_view path) const;

  /// JSON type of a leaf setting ("string", "integer", ...), empty when the path is unknown.
  [[nodiscard]] std::string type_of(std::string_view path) const;

 private:
  nlohmann::json document_;
  std::set<std::string> secrets_;
  std::set<std::string> reloadable_;
  std::map<std::string, std::string, std::less<>> types_;
};

/// Where the configuration comes from: a YAML file and the environment.
struct Sources {
  std::optional<std::filesystem::path> file;
  /// Variables PSIM__<SECTION>__<KEY>=value override settings (PSIM__LOG__LEVEL=debug -> log.level).
  std::map<std::string, std::string> environment;
};

/// Snapshot of the environment of the process.
[[nodiscard]] std::map<std::string, std::string> process_environment();

/// A validated configuration with defaults applied. Secrets are kept apart from the document,
/// so logging the document never reveals them.
class Config {
 public:
  Config() = default;

  Config(nlohmann::json document, std::map<std::string, std::string, std::less<>> secrets)
      : document_(std::move(document)), secrets_(std::move(secrets)) {}

  [[nodiscard]] const nlohmann::json& document() const noexcept { return document_; }

  /// Value at a dotted path; the schema guarantees presence and type for required settings.
  template <typename T>
  [[nodiscard]] T get(std::string_view path) const {
    return document_.at(pointer(path)).get<T>();
  }

  [[nodiscard]] bool has(std::string_view path) const { return document_.contains(pointer(path)); }

  /// Content of a secret file named by a setting marked x-psim-secret.
  [[nodiscard]] std::optional<std::string_view> secret(std::string_view path) const;

 private:
  static nlohmann::json::json_pointer pointer(std::string_view path);

  nlohmann::json document_;
  std::map<std::string, std::string, std::less<>> secrets_;
};

[[nodiscard]] std::expected<Config, Problems> load(const Schema& schema, const Sources& sources);

/// Settings that changed between two configurations: applied at run time or needing a restart.
struct ReloadPlan {
  std::vector<std::string> apply;
  std::vector<std::string> restart_required;
};

[[nodiscard]] ReloadPlan plan_reload(const Schema& schema, const Config& current, const Config& updated);

}  // namespace psim::platform::config
