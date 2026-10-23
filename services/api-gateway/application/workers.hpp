#pragma once
#include <memory>

#include "psim/platform/runtime.hpp"

namespace psim::api_gateway {
std::unique_ptr<platform::runtime::Component> gateway(const platform::runtime::Context& context);
}
