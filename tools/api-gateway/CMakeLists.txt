add_executable(psim-api-probe probe.cpp)
psim_target_defaults(psim-api-probe)
target_link_libraries(psim-api-probe PRIVATE psim::platform_stream psim::platform_redis)
