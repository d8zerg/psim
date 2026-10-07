# psim::platform_log - structured JSON logging to stdout (step 2.1, ADR-039, crosscutting.md 6.3).
add_library(psim_platform_log STATIC src/log.cpp)
add_library(psim::platform_log ALIAS psim_platform_log)
target_include_directories(psim_platform_log PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
psim_target_defaults(psim_platform_log)

psim_add_test(psim_platform_log_test
  SOURCES tests/log_test.cpp
  LIBS psim::platform_log)
