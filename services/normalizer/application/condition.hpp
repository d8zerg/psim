#pragma once
#include <string_view>

#include "psim/ingest/v1/raw_event.pb.h"
#include "psim/platform/error.hpp"

namespace psim::normalizer {
/// Bounded string/boolean CEL subset over raw fields; unsupported syntax fails validation.
platform::Result<bool> condition(std::string_view expression, const ingest::v1::RawEvent& raw);
}  // namespace psim::normalizer
