#pragma once

#include "psim/platform/config.hpp"
#include "psim/platform/redis/pool.hpp"

namespace psim::platform::stream {
/// Transport and credentials shared by service profiles; timeouts remain owned by the caller.
[[nodiscard]] redis::PoolOptions redis_options(const config::Config& config);
}  // namespace psim::platform::stream
