# Template of a PSIM service (step 2.1, ADR-039): `task service:new NAME=<name> CONTEXTS=<ctx>` copies it
# into services/<name>. It builds and runs as is, so the template itself proves what a new service
# gets: configuration, logs, metrics, traces, health endpoints and graceful drain (FF-06).
add_library(psim_service_template_core STATIC src/worker.cpp)
target_include_directories(psim_service_template_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(psim_service_template_core PUBLIC psim::platform_runtime)
psim_target_defaults(psim_service_template_core)

psim_add_service(service-template
  SOURCES src/main.cpp
  LIBS psim_service_template_core
  SUMMARY "PSIM service template"
  NO_PACKAGE)

psim_add_test(psim_service_template_test
  SOURCES tests/worker_test.cpp
  LIBS psim_service_template_core)
