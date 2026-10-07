#include "psim/platform/error.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

#include "psim/platform/error_codes.hpp"

namespace psim::platform {

namespace {

// The catalog is generated in enumerator order, so a code indexes its own row.
consteval bool rows_in_enumerator_order() {
  for (std::size_t i = 0; i < kErrorCodes.size(); ++i) {
    if (static_cast<std::size_t>(kErrorCodes.at(i).code) != i) {
      return false;
    }
  }
  return true;
}

static_assert(rows_in_enumerator_order());

}  // namespace

// Every enumerator has a row (checked above), so the index is always in range.
const ErrorCodeInfo& info(ErrorCode code) noexcept {
  return kErrorCodes[static_cast<std::size_t>(code)];
}

std::optional<ErrorCode> parse_error_code(std::string_view name) noexcept {
  const auto* row = std::ranges::find(kErrorCodes, name, &ErrorCodeInfo::name);
  if (row == kErrorCodes.end()) {
    return std::nullopt;
  }
  return row->code;
}

}  // namespace psim::platform
