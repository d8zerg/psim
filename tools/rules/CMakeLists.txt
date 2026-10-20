add_executable(psim-rules-probe probe.cpp)
psim_target_defaults(psim-rules-probe)
target_link_libraries(psim-rules-probe PRIVATE psim::platform_stream psim::platform_clickhouse)
