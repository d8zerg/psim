#include "psim/platform/config.hpp"

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

extern char**
    environ;  // NOLINT(readability-redundant-declaration,cppcoreguidelines-avoid-non-const-global-variables): POSIX

namespace psim::platform::config {

using nlohmann::json;

namespace {

constexpr std::string_view kEnvPrefix = "PSIM__";

std::string join(std::string_view prefix, std::string_view key) {
  return prefix.empty() ? std::string(key) : std::string(prefix) + "." + std::string(key);
}

json::json_pointer to_pointer(std::string_view path) {
  std::string text;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto dot = path.find('.', start);
    const auto part = path.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
    text += '/';
    text += part;
    if (dot == std::string_view::npos) {
      break;
    }
    start = dot + 1;
  }
  return json::json_pointer(text);
}

std::string to_dotted(const json::json_pointer& pointer) {
  std::string path = pointer.to_string();
  if (!path.empty()) {
    path.erase(0, 1);
  }
  std::ranges::replace(path, '/', '.');
  return path;
}

// YAML scalars carry no type; plain scalars that read as booleans or numbers become them, quoted
// ones stay strings. The schema then checks the result.
json scalar(const YAML::Node& node) {
  const std::string& text = node.Scalar();
  if (node.Tag() == "!") {
    return text;
  }
  if (text == "true" || text == "false") {
    return text == "true";
  }
  if (text == "null" || text == "~") {
    return nullptr;
  }
  std::int64_t integer = 0;
  const char* end =
      text.data() + text.size();  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): from_chars range
  if (auto [ptr, ec] = std::from_chars(text.data(), end, integer); ec == std::errc{} && ptr == end) {
    return integer;
  }
  double number = 0;
  if (auto [ptr, ec] = std::from_chars(text.data(), end, number); ec == std::errc{} && ptr == end && !text.empty()) {
    return number;
  }
  return text;
}

json to_json(const YAML::Node& node) {
  switch (node.Type()) {
    case YAML::NodeType::Map: {
      json object = json::object();
      for (const auto& item : node) {
        object[item.first.as<std::string>()] = to_json(item.second);
      }
      return object;
    }
    case YAML::NodeType::Sequence: {
      json array = json::array();
      for (const auto& item : node) {
        array.push_back(to_json(item));
      }
      return array;
    }
    case YAML::NodeType::Scalar:
      return scalar(node);
    case YAML::NodeType::Null:
    case YAML::NodeType::Undefined:
      return nullptr;
  }
  return nullptr;
}

std::optional<json> typed(const std::string& type, const std::string& text) {
  const char* end =
      text.data() + text.size();  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): from_chars range
  if (type == "string") {
    return text;
  }
  if (type == "boolean" && (text == "true" || text == "false")) {
    return text == "true";
  }
  if (type == "integer") {
    std::int64_t value = 0;
    if (auto [ptr, ec] = std::from_chars(text.data(), end, value); ec == std::errc{} && ptr == end) {
      return value;
    }
  }
  if (type == "number") {
    double value = 0;
    if (auto [ptr, ec] = std::from_chars(text.data(), end, value); ec == std::errc{} && ptr == end) {
      return value;
    }
  }
  return std::nullopt;
}

// Missing sections are created so that the defaults inside them apply; a created section that
// stays empty and is not required is removed again.
void apply_defaults(const json& schema, json& node) {
  if (!schema.contains("properties") || !node.is_object()) {
    return;
  }
  const auto required = schema.value("required", json::array());
  for (const auto& [key, property] : schema.at("properties").items()) {
    if (!node.contains(key)) {
      if (property.contains("default")) {
        node[key] = property.at("default");
      } else if (property.contains("properties")) {
        node[key] = json::object();
        apply_defaults(property, node[key]);
        // NOLINTNEXTLINE(modernize-use-ranges): nlohmann::json iterators do not model ranges
        if (node[key].empty() && std::find(required.begin(), required.end(), key) == required.end()) {
          node.erase(key);
        }
        continue;
      }
    }
    if (node.contains(key)) {
      apply_defaults(property, node[key]);
    }
  }
}

class Collector final : public nlohmann::json_schema::basic_error_handler {
 public:
  explicit Collector(Problems& problems) : problems_(&problems) {}

  void error(const json::json_pointer& pointer, const json& /*instance*/, const std::string& message) override {
    basic_error_handler::error(pointer, nullptr, message);
    problems_->push_back(Problem{.path = to_dotted(pointer), .message = message});
  }

 private:
  Problems* problems_;
};

std::optional<std::string> read_secret(const std::string& path, const std::string& setting, Problems& problems) {
  struct stat info{};
  if (::stat(path.c_str(), &info) != 0) {
    problems.push_back({setting, "secret file " + path + " is not readable"});
    return std::nullopt;
  }
  if ((info.st_mode & 077U) != 0) {
    problems.push_back({setting, "secret file " + path + " is accessible to group or others; use mode 0400 (SR-09)"});
    return std::nullopt;
  }
  if (info.st_uid != ::geteuid()) {
    problems.push_back({setting, "secret file " + path + " is not owned by the service user (SR-09)"});
    return std::nullopt;
  }
  const std::ifstream file(path, std::ios::binary);
  std::ostringstream content;
  content << file.rdbuf();
  std::string secret = content.str();
  while (!secret.empty() && (secret.back() == '\n' || secret.back() == '\r')) {
    secret.pop_back();
  }
  return secret;
}

void collect_changes(const json& before, const json& after, const std::string& prefix, std::vector<std::string>& out) {
  if (before.is_object() && after.is_object()) {
    for (const auto& [key, value] : before.items()) {
      collect_changes(value, after.contains(key) ? after.at(key) : json(), join(prefix, key), out);
    }
    for (const auto& [key, value] : after.items()) {
      if (!before.contains(key)) {
        out.push_back(join(prefix, key));
      }
    }
  } else if (before != after) {
    out.push_back(prefix);
  }
}

}  // namespace

std::expected<Schema, Problems> Schema::parse(std::string_view json_text) {
  Schema schema;
  try {
    schema.document_ = json::parse(json_text);
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(schema.document_);
  } catch (const std::exception& e) {
    return std::unexpected(Problems{{"", std::string("invalid configuration schema: ") + e.what()}});
  }
  // Annotations and leaf types by dotted path.
  auto walk = [&schema](const auto& self, const json& node, const std::string& prefix) -> void {
    if (!node.contains("properties")) {
      return;
    }
    for (const auto& [key, property] : node.at("properties").items()) {
      const auto path = join(prefix, key);
      if (property.value("x-psim-secret", false)) {
        schema.secrets_.insert(path);
      }
      if (property.value("x-psim-reload", false)) {
        schema.reloadable_.insert(path);
      }
      if (property.contains("type") && property.at("type").is_string()) {
        schema.types_[path] = property.at("type").get<std::string>();
      }
      self(self, property, path);
    }
  };
  walk(walk, schema.document_, "");
  return schema;
}

bool Schema::reloadable(std::string_view path) const {
  std::string current(path);
  while (true) {
    if (reloadable_.contains(current)) {
      return true;
    }
    const auto dot = current.rfind('.');
    if (dot == std::string::npos) {
      return false;
    }
    current.resize(dot);
  }
}

std::string Schema::type_of(std::string_view path) const {
  const auto it = types_.find(path);
  return it == types_.end() ? std::string() : it->second;
}

std::map<std::string, std::string> process_environment() {
  std::map<std::string, std::string> result;
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {  // NOLINT: POSIX environ array
    const std::string_view text(*entry);
    const auto equals = text.find('=');
    if (equals != std::string_view::npos) {
      result.emplace(text.substr(0, equals), text.substr(equals + 1));
    }
  }
  return result;
}

std::optional<std::string_view> Config::secret(std::string_view path) const {
  const auto it = secrets_.find(path);
  if (it == secrets_.end()) {
    return std::nullopt;
  }
  return it->second;
}

json::json_pointer Config::pointer(std::string_view path) {
  return to_pointer(path);
}

std::expected<Config, Problems> load(const Schema& schema, const Sources& sources) {
  Problems problems;
  json document = json::object();
  if (sources.file) {
    try {
      document = to_json(YAML::LoadFile(sources.file->string()));
      if (document.is_null()) {
        document = json::object();
      }
    } catch (const YAML::Exception& e) {
      return std::unexpected(Problems{{"", "cannot read " + sources.file->string() + ": " + e.what()}});
    }
  }

  for (const auto& [name, value] : sources.environment) {
    if (!name.starts_with(kEnvPrefix)) {
      continue;
    }
    std::string path = name.substr(kEnvPrefix.size());
    std::ranges::transform(path, path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto pos = path.find("__"); pos != std::string::npos; pos = path.find("__", pos + 1)) {
      path.replace(pos, 2, ".");
    }
    const auto type = schema.type_of(path);
    if (schema.secret_paths().contains(path)) {
      problems.push_back(
          {path, std::format("{}: secrets are read from files only, not from the environment (SR-09)", name)});
    } else if (type.empty() || type == "object" || type == "array") {
      problems.push_back({path, std::format("{}: no such setting", name)});
    } else if (auto converted = typed(type, value)) {
      document[to_pointer(path)] = *converted;
    } else {
      problems.push_back({path, std::format("{}: expected {}", name, type)});
    }
  }

  apply_defaults(schema.document(), document);
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(schema.document());
  Collector collector(problems);
  (void)validator.validate(document, collector);

  std::map<std::string, std::string, std::less<>> secrets;
  for (const auto& path : schema.secret_paths()) {
    const auto pointer = to_pointer(path);
    if (document.contains(pointer) && document.at(pointer).is_string()) {
      if (auto secret = read_secret(document.at(pointer).get<std::string>(), path, problems)) {
        secrets.emplace(path, std::move(*secret));
      }
    }
  }
  if (!problems.empty()) {
    return std::unexpected(std::move(problems));
  }
  return Config(std::move(document), std::move(secrets));
}

ReloadPlan plan_reload(const Schema& schema, const Config& current, const Config& updated) {
  std::vector<std::string> changed;
  collect_changes(current.document(), updated.document(), "", changed);
  for (const auto& path : schema.secret_paths()) {
    if (current.secret(path) != updated.secret(path) && std::ranges::find(changed, path) == changed.end()) {
      changed.push_back(path);
    }
  }
  ReloadPlan plan;
  for (auto& path : changed) {
    (schema.reloadable(path) ? plan.apply : plan.restart_required).push_back(std::move(path));
  }
  return plan;
}

}  // namespace psim::platform::config
