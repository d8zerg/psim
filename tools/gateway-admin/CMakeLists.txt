add_executable(psim-gateway-admin main.cpp)
psim_target_defaults(psim-gateway-admin)
target_link_libraries(psim-gateway-admin PRIVATE psim::platform_connector)
install(TARGETS psim-gateway-admin RUNTIME DESTINATION bin COMPONENT connector-gateway)
