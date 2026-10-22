add_executable(psim-report-probe probe.cpp)
psim_target_defaults(psim-report-probe)
target_link_libraries(psim-report-probe PRIVATE psim::platform_stream)
