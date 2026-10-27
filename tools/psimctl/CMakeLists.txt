# psimctl - administration CLI (ADR-001).
find_package(nlohmann_json REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)
find_package(RdKafka REQUIRED CONFIG)
add_library(psimctl_operations STATIC src/common.cpp src/operations.cpp src/backup.cpp src/kafka.cpp)
psim_target_defaults(psimctl_operations)
target_include_directories(psimctl_operations PUBLIC src)
target_link_libraries(psimctl_operations PUBLIC psim::platform_files psim::platform_s3 psim::platform_stream psim::platform_clickhouse
  nlohmann_json::nlohmann_json PRIVATE OpenSSL::Crypto RdKafka::rdkafka)
add_executable(psimctl src/main.cpp)
psim_target_defaults(psimctl)
target_link_libraries(psimctl PRIVATE psim::platform_core psim::contracts psimctl_operations)
install(TARGETS psimctl RUNTIME DESTINATION bin COMPONENT psimctl)
psim_package(psimctl SUMMARY "PSIM administration CLI")
set_property(GLOBAL PROPERTY PSIM_PACKAGE_DEPENDS_psimctl
  "ch-migrate;psim-pg-migrate;audit-tool;connector-gateway;projection-service;response-engine")
set_property(GLOBAL PROPERTY PSIM_SYSTEM_PACKAGE_DEPENDS_psimctl
  "openssl, tar")
set_property(GLOBAL PROPERTY PSIM_SYSTEM_PACKAGE_RECOMMENDS_psimctl "postgresql-client-18")

# Smoke tests: every command also runs under the sanitizer and coverage presets. A pass regex
# ignores the exit code, so sanitizer reports are matched explicitly.
function(psimctl_smoke_test name expected)
  add_test(NAME psimctl.${name} COMMAND psimctl ${ARGN})
  set_tests_properties(psimctl.${name} PROPERTIES
    LABELS "smoke;psimctl"
    PASS_REGULAR_EXPRESSION "${expected}"
    FAIL_REGULAR_EXPRESSION "Sanitizer|runtime error:")
endfunction()

string(REPLACE "+" "\\+" PSIM_VERSION_REGEX "${PSIM_VERSION}")
psimctl_smoke_test(version "^psimctl ${PSIM_VERSION_REGEX} " version)
psimctl_smoke_test(envelope_sample "\"messageId\"" contracts envelope-sample)
psimctl_smoke_test(usage "^usage: psimctl" unknown)
psim_add_test(psimctl_operations_test SOURCES tests/operations_test.cpp LIBS psimctl_operations)
