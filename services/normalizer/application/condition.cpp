#include "condition.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "psim/domain/processing/mapping.hpp"
#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/error_codes.hpp"

namespace psim::normalizer {
platform::Result<bool> condition(std::string_view expression, const ingest::v1::RawEvent& raw) {
  auto result = domain::processing::mapping_condition(
      expression, {raw.raw_code(), raw.source_ref(), [&raw](std::string_view name) -> std::optional<std::string> {
                     const auto found = raw.attributes().find(std::string(name));
                     return found == raw.attributes().end() ? std::nullopt : std::optional<std::string>(found->second);
                   }});
  if (!result) {
    return platform::fail(platform::ErrorCode::kProcessingPayloadInvalid, result.error());
  }
  return *result;
}
}  // namespace psim::normalizer
