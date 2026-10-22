add_executable(psim-command-probe probe.cpp)
psim_target_defaults(psim-command-probe)
target_link_libraries(psim-command-probe PRIVATE psim::platform_stream psim::platform_connector)
