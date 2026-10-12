#pragma once

#include <string>
#include <string_view>

#include "psim/platform/error.hpp"

namespace psim::platform::pg::detail {
[[nodiscard]] bool identifier(std::string_view value);
/// SQL templates contain @schema@ only in identifier positions. Validate before substitution;
/// all data values still use PostgreSQL parameters (SR-13).
[[nodiscard]] Result<std::string> schema_sql(std::string_view schema, std::string_view sql);
[[nodiscard]] std::string fingerprint(std::string_view value);
}  // namespace psim::platform::pg::detail
