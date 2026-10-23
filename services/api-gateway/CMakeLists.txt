add_library(psim_api_gateway_core STATIC application/routes.cpp application/metrics.cpp application/realtime.cpp adapters/proxy.cpp adapters/server.cpp)
target_include_directories(psim_api_gateway_core PUBLIC application adapters)
target_link_libraries(psim_api_gateway_core PUBLIC psim::platform_stream psim::platform_authz)
psim_target_defaults(psim_api_gateway_core)
psim_add_service(api-gateway SOURCES src/main.cpp LIBS psim_api_gateway_core SUMMARY "PSIM API Gateway")

psim_add_test(psim_api_gateway_test SOURCES tests/routes_test.cpp tests/realtime_test.cpp LIBS psim_api_gateway_core)
