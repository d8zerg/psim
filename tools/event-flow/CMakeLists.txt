add_executable(psim-event-flow-probe probe.cpp)
target_link_libraries(psim-event-flow-probe PRIVATE psim::platform_stream)
psim_target_defaults(psim-event-flow-probe)
