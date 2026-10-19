add_executable(psim-event-flow-probe probe.cpp)
target_link_libraries(psim-event-flow-probe PRIVATE psim::platform_stream)
psim_target_defaults(psim-event-flow-probe)

add_executable(psim-flow-simulator simulator.cpp)
target_link_libraries(psim-flow-simulator PRIVATE psim::platform_stream)
psim_target_defaults(psim-flow-simulator)
