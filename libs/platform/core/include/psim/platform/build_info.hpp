#pragma once

#include <string_view>

namespace psim::platform {

// Identification of the running binary: reported by health endpoints, logs and psimctl.
struct BuildInfo {
  std::string_view version;
  std::string_view git_commit;
  std::string_view compiler;
  std::string_view build_type;
};

[[nodiscard]] BuildInfo build_info() noexcept;

}  // namespace psim::platform
