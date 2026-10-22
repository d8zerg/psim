#pragma once
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace psim::domain::processing {
struct MappingInput {
  std::string_view raw_code{};
  std::string_view source_ref{};
  std::function<std::optional<std::string>(std::string_view)> attribute{};
};

/// The same bounded CEL subset validates published mappings and evaluates raw events.
std::expected<bool, std::string> mapping_condition(std::string_view expression, const MappingInput& raw = {});
}  // namespace psim::domain::processing
