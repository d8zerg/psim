# Connector SDK; no dependency on Kafka, PostgreSQL, the stream runtime or services.
find_package(nlohmann_json REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)
add_library(psim_platform_connector STATIC src/store.cpp src/client.cpp src/state.cpp src/grpc_process.cpp)
add_library(psim::platform_connector ALIAS psim_platform_connector)
target_include_directories(psim_platform_connector PUBLIC include)
target_link_libraries(psim_platform_connector PUBLIC psim::contracts psim::platform_security PRIVATE nlohmann_json::nlohmann_json)
psim_target_defaults(psim_platform_connector)
psim_add_test(psim_connector_test SOURCES tests/main.cpp tests/store_test.cpp tests/state_test.cpp tests/client_test.cpp
  LIBS psim::platform_connector nlohmann_json::nlohmann_json)
add_executable(psim-connector-example examples/basic.cpp)
target_link_libraries(psim-connector-example PRIVATE psim::platform_connector)
psim_target_defaults(psim-connector-example)
