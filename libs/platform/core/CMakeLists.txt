# psim::platform_core - build information shared by all services and tools.

# Git revision of the sources; no timestamps, so the same commit builds identical artifacts.
execute_process(
  COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --short=12 HEAD
  OUTPUT_VARIABLE PSIM_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT PSIM_GIT_COMMIT)
  set(PSIM_GIT_COMMIT "unknown")
endif()
configure_file(src/build_config.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/gen/psim/platform/build_config.hpp" @ONLY)

add_library(psim_platform_core STATIC src/build_info.cpp)
add_library(psim::platform_core ALIAS psim_platform_core)
target_include_directories(psim_platform_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gen")
psim_target_defaults(psim_platform_core)

psim_add_test(psim_platform_core_test
  SOURCES tests/build_info_test.cpp tests/static_runtime_test.cpp
  LIBS psim::platform_core)
