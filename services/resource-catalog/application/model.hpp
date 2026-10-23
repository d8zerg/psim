#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/error.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::resource_catalog {
namespace p = platform;
using Json = nlohmann::json;
using Time = std::chrono::system_clock::time_point;
inline constexpr std::size_t kMaxImageBytes = std::size_t{20} * 1024 * 1024;
inline constexpr std::uint64_t kMaxImagePixels = 16000000;
inline constexpr std::size_t kMaxImportRows = 100000;

struct Resource {
  std::string kind{};
  std::string id{};
  std::string site{};
  std::string parent{};
  std::string code{};
  std::string connector{};
  std::string external{};
  std::uint64_t version = 0;
  Json body{};
};

struct Page {
  std::vector<Json> items;
  std::string next;
};

[[nodiscard]] Json read_json(std::string_view bytes);
[[nodiscard]] p::Result<Resource> parse(std::string kind, const Json& input);
[[nodiscard]] bool allowed(const p::security::Principal& principal, std::string_view site, bool write = false,
                           std::string_view zone = {});
[[nodiscard]] std::string timestamp(Time now);
[[nodiscard]] p::Result<Time> time(std::string_view value);
[[nodiscard]] p::Result<std::vector<Json>> import_rows(std::string_view format, std::string_view bytes);
[[nodiscard]] std::string export_rows(std::string_view format, const std::vector<Json>& rows);
[[nodiscard]] p::Result<std::string> maintenance_transition(Json& body, Time now);
[[nodiscard]] p::Result<void> validate_maintenance(const Json& body, Time now);
[[nodiscard]] p::Result<void> validate_type(const Json& body);
[[nodiscard]] bool canonical_uuid(std::string_view value);
}  // namespace psim::resource_catalog
