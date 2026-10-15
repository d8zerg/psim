add_executable(psim-simulator main.cpp)
target_link_libraries(psim-simulator PRIVATE psim::platform_stream)
psim_target_defaults(psim-simulator)
