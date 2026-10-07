# Services on the platform runtime (step 2.1, ADR-039).
#
# psim_add_service(<name> SOURCES ... [LIBS ...] [SUMMARY <text>] [NO_PACKAGE])
#   Executable psim-<name> linked with psim::platform_runtime; installed into bin/ and packaged as
#   psim-<name> (Debian) unless NO_PACKAGE. Smoke tests check --version and the configuration
#   schema (--check-config with the defaults).

function(psim_add_service name)
  cmake_parse_arguments(ARG "NO_PACKAGE" "SUMMARY" "SOURCES;LIBS" ${ARGN})
  set(target "psim-${name}")
  add_executable(${target} ${ARG_SOURCES})
  psim_target_defaults(${target})
  target_link_libraries(${target} PRIVATE psim::platform_runtime ${ARG_LIBS})
  if(NOT ARG_NO_PACKAGE)
    install(TARGETS ${target} RUNTIME DESTINATION bin COMPONENT ${name})
    psim_package(${name} SUMMARY "${ARG_SUMMARY}")
  endif()
  add_test(NAME ${target}.version COMMAND ${target} --version)
  add_test(NAME ${target}.check_config COMMAND ${target} --check-config)
  set_tests_properties(${target}.version ${target}.check_config PROPERTIES
    LABELS "smoke;${target}"
    FAIL_REGULAR_EXPRESSION "Sanitizer|runtime error:")
endfunction()
