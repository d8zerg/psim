# Native analytical adapter and durable Kafka batch loader (ADR-028).
find_package(clickhouse-cpp REQUIRED CONFIG)
find_package(RdKafka REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(prometheus-cpp REQUIRED CONFIG)
add_library(psim_platform_clickhouse STATIC src/client.cpp src/detail.cpp src/metrics.cpp src/loader.cpp src/migrations.cpp)
add_library(psim::platform_clickhouse ALIAS psim_platform_clickhouse)
target_include_directories(psim_platform_clickhouse PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_clickhouse PUBLIC psim::platform_async psim::platform_errors
  psim::platform_security psim::platform_kafka psim::platform_pg clickhouse-cpp-lib::clickhouse-cpp-lib
  PRIVATE OpenSSL::Crypto prometheus-cpp::core nlohmann_json::nlohmann_json)
psim_target_defaults(psim_platform_clickhouse)
psim_add_test(psim_platform_clickhouse_test SOURCES tests/value_test.cpp LIBS psim::platform_clickhouse psim::contracts)
psim_add_component_test(psim_platform_clickhouse_component_test SOURCES tests/component_test.cpp tests/crash_test.cpp LIBS psim::platform_clickhouse psim::contracts RdKafka::rdkafka)

add_executable(psim_clickhouse_test_worker tests/worker.cpp)
psim_target_defaults(psim_clickhouse_test_worker)
target_link_libraries(psim_clickhouse_test_worker PRIVATE psim::platform_clickhouse psim::contracts GTest::gtest)
target_compile_definitions(psim_platform_clickhouse_component_test PRIVATE PSIM_CH_TEST_WORKER="$<TARGET_FILE:psim_clickhouse_test_worker>")
add_dependencies(psim_platform_clickhouse_component_test psim_clickhouse_test_worker)
add_executable(psim_clickhouse_throughput benchmarks/throughput.cpp)
psim_target_defaults(psim_clickhouse_throughput)
target_link_libraries(psim_clickhouse_throughput PRIVATE psim::platform_clickhouse)
