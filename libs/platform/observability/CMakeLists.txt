# psim::platform_observability - metrics (Prometheus text format) and tracing (OpenTelemetry, OTLP
# gRPC) of every service (step 2.1, ADR-039, crosscutting.md section 6).
find_package(prometheus-cpp REQUIRED CONFIG)
find_package(opentelemetry-cpp REQUIRED CONFIG)

add_library(psim_platform_observability STATIC src/metrics.cpp src/tracing.cpp)
add_library(psim::platform_observability ALIAS psim_platform_observability)
target_include_directories(psim_platform_observability PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_observability
  PUBLIC prometheus-cpp::core opentelemetry-cpp::api opentelemetry-cpp::trace psim::platform_log
  PRIVATE opentelemetry-cpp::exporter_otlp_grpc opentelemetry-cpp::resources)
psim_target_defaults(psim_platform_observability)

psim_add_test(psim_platform_observability_test
  SOURCES tests/observability_test.cpp
  LIBS psim::platform_observability opentelemetry-cpp::exporter_in_memory)
