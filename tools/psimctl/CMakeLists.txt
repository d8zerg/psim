# psimctl - administration CLI (ADR-001); step 9.5 adds operations, step 1.1 provides the skeleton.
add_executable(psimctl src/main.cpp)
psim_target_defaults(psimctl)
target_link_libraries(psimctl PRIVATE psim::platform_core psim::contracts)
install(TARGETS psimctl RUNTIME DESTINATION bin)
