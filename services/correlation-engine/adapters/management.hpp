#pragma once

#include <memory>
#include <string>

#include "psim/platform/error.hpp"
#include "psim/platform/runtime.hpp"

namespace psim::correlation::management {
std::string settings();
platform::Result<std::unique_ptr<platform::runtime::Component>> create(const platform::runtime::Context& context);
}  // namespace psim::correlation::management
