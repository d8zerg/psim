#pragma once

#include <cstddef>
#include <memory>

#include "psim/platform/runtime.hpp"

namespace psim::correlation::live {
std::unique_ptr<platform::runtime::Component> router(const platform::runtime::Context& context);
std::unique_ptr<platform::runtime::Component> engine(const platform::runtime::Context& context, std::size_t shard);
}  // namespace psim::correlation::live
