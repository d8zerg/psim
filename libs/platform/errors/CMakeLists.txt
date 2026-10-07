# psim::platform_errors - error model of the platform (step 2.1, ADR-039): codes from the catalog
# contracts/errors/errors.yaml, Error, Result<T>.

set(gen "${CMAKE_CURRENT_BINARY_DIR}/gen/psim/platform/error_codes.hpp")
set(catalog "${PROJECT_SOURCE_DIR}/contracts/errors/errors.yaml")
set(generator "${PROJECT_SOURCE_DIR}/tools/cpp/gen_error_codes.py")
add_custom_command(
  OUTPUT "${gen}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/gen/psim/platform"
  COMMAND python3 -I "${generator}" "${catalog}" "${gen}"
  DEPENDS "${catalog}" "${generator}"
  COMMENT "Generating error codes from the catalog")

add_library(psim_platform_errors STATIC src/error.cpp "${gen}")
add_library(psim::platform_errors ALIAS psim_platform_errors)
target_include_directories(psim_platform_errors
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/gen")
psim_target_defaults(psim_platform_errors)

psim_add_test(psim_platform_errors_test
  SOURCES tests/error_test.cpp
  LIBS psim::platform_errors)
