add_executable(psim-processing-probe probe.cpp)
psim_target_defaults(psim-processing-probe)
target_link_libraries(psim-processing-probe PRIVATE psim::platform_stream)
