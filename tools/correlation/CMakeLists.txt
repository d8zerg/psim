find_package(RdKafka REQUIRED CONFIG)
add_executable(psim-correlation-probe probe.cpp)
psim_target_defaults(psim-correlation-probe)
target_link_libraries(psim-correlation-probe PRIVATE psim::platform_stream RdKafka::rdkafka)
