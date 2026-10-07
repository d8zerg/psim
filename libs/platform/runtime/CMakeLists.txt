# psim::platform_runtime - lifecycle of a service process (step 2.1, ADR-039): configuration,
# logging, metrics and tracing, admin HTTP endpoints (/health/live, /health/ready, /metrics, /info),
# OS signals, graceful shutdown with drain.
find_package(Boost REQUIRED CONFIG)

add_library(psim_platform_runtime STATIC src/runtime.cpp src/admin_server.cpp src/schema.cpp src/main.cpp)
add_library(psim::platform_runtime ALIAS psim_platform_runtime)
target_include_directories(psim_platform_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_runtime
  PUBLIC Boost::headers psim::platform_config psim::platform_errors psim::platform_log
         psim::platform_observability psim::platform_core)
psim_target_defaults(psim_platform_runtime)

psim_add_test(psim_platform_runtime_test
  SOURCES tests/runtime_test.cpp
  LIBS psim::platform_runtime)
