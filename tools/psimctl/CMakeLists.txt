# psimctl - administration CLI (ADR-001); step 9.5 adds operations, step 1.1 provides the skeleton.
add_executable(psimctl src/main.cpp)
psim_target_defaults(psimctl)
target_link_libraries(psimctl PRIVATE psim::platform_core psim::contracts)
install(TARGETS psimctl RUNTIME DESTINATION bin)

# Smoke tests: every command also runs under the sanitizer and coverage presets. A pass regex
# ignores the exit code, so sanitizer reports are matched explicitly.
function(psimctl_smoke_test name expected)
  add_test(NAME psimctl.${name} COMMAND psimctl ${ARGN})
  set_tests_properties(psimctl.${name} PROPERTIES
    LABELS smoke
    PASS_REGULAR_EXPRESSION "${expected}"
    FAIL_REGULAR_EXPRESSION "Sanitizer|runtime error:")
endfunction()

psimctl_smoke_test(version "^psimctl ${PROJECT_VERSION} " version)
psimctl_smoke_test(envelope_sample "\"messageId\"" contracts envelope-sample)
psimctl_smoke_test(usage "^usage: psimctl" unknown)
