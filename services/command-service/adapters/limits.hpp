#pragma once
#include <cstddef>

namespace psim::command_service {
// Leave room for routing fields within the connector's 32 KiB command frame.
inline constexpr std::size_t kRequestMaxBytes = 30000;
}  // namespace psim::command_service
