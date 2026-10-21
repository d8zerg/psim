add_executable(psim-incident-probe probe.cpp)
psim_target_defaults(psim-incident-probe)
target_link_libraries(psim-incident-probe PRIVATE psim::platform_stream)
