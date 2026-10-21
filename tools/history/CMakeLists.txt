add_executable(psim-history-probe probe.cpp)
psim_target_defaults(psim-history-probe)
target_link_libraries(psim-history-probe PRIVATE psim::platform_stream)
