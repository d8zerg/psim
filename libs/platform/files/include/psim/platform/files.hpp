#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"

namespace psim::platform::files {
Result<std::string> read(const std::filesystem::path& path, std::size_t maximum = std::size_t{16} * 1024 * 1024);
Result<void> sync(const std::filesystem::path& path);
/// Serialize appenders, persist the record and its directory before acknowledging delivery.
Result<void> append(const std::filesystem::path& path, std::string_view body);
}  // namespace psim::platform::files
