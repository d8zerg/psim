# psim::platform_config - service configuration (step 2.1, ADR-039, crosscutting.md section 5):
# YAML file and PSIM__* environment overrides, JSON Schema validation reporting every problem,
# secrets read from protected files (SR-09), partial hot reload.
find_package(yaml-cpp REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(nlohmann_json_schema_validator REQUIRED CONFIG)

add_library(psim_platform_config STATIC src/config.cpp)
add_library(psim::platform_config ALIAS psim_platform_config)
target_include_directories(psim_platform_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_config
  PUBLIC nlohmann_json::nlohmann_json
  PRIVATE yaml-cpp::yaml-cpp nlohmann_json_schema_validator)
psim_target_defaults(psim_platform_config)

psim_add_test(psim_platform_config_test
  SOURCES tests/config_test.cpp
  LIBS psim::platform_config)
