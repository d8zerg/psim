#include "psim/platform/build_info.hpp"

#include "psim/platform/build_config.hpp"

namespace psim::platform {

BuildInfo build_info() noexcept {
  return BuildInfo{
      .version = PSIM_VERSION,
      .git_commit = PSIM_GIT_COMMIT,
      .compiler = PSIM_COMPILER,
      .build_type = PSIM_BUILD_TYPE,
  };
}

}  // namespace psim::platform
