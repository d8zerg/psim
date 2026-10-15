find_package(nlohmann_json REQUIRED CONFIG)
add_executable(psim-connector-conformance main.cpp)
target_link_libraries(psim-connector-conformance PRIVATE psim::platform_connector nlohmann_json::nlohmann_json)
psim_target_defaults(psim-connector-conformance)
