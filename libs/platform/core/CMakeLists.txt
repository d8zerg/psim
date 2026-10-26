# psim::platform_core - build information shared by all services and tools.

# Version and revision are derived once for every component by PsimVersion.
configure_file(src/build_config.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/gen/psim/platform/build_config.hpp" @ONLY)

add_library(psim_platform_core STATIC src/build_info.cpp)
add_library(psim::platform_core ALIAS psim_platform_core)
target_include_directories(psim_platform_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gen")
psim_target_defaults(psim_platform_core)

psim_add_test(psim_platform_core_test
  SOURCES tests/build_info_test.cpp tests/runtime_unwind_test.cpp
  LIBS psim::platform_core)
