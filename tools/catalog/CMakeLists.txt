add_executable(psim-catalog-observer observer.cpp)
psim_target_defaults(psim-catalog-observer)
target_link_libraries(psim-catalog-observer PRIVATE psim::platform_stream)
