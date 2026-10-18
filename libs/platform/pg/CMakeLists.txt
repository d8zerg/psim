# PostgreSQL adapter (ADR-014): libpq, transactions, migrations, inbox and outbox.
find_package(RdKafka REQUIRED CONFIG)
find_package(PostgreSQL REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
add_library(psim_platform_pg STATIC src/connection.cpp src/pool.cpp src/transaction.cpp src/sql.cpp src/migrations.cpp src/store.cpp)
add_library(psim::platform_pg ALIAS psim_platform_pg)
target_include_directories(psim_platform_pg PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_pg PUBLIC psim::platform_async psim::platform_errors psim::platform_kafka
  PRIVATE PostgreSQL::PostgreSQL OpenSSL::Crypto nlohmann_json::nlohmann_json)
psim_target_defaults(psim_platform_pg)

psim_add_test(psim_platform_pg_test SOURCES tests/value_test.cpp LIBS psim::platform_pg)
psim_add_component_test(psim_platform_pg_component_test SOURCES tests/component_test.cpp tests/crash_test.cpp LIBS psim::platform_pg RdKafka::rdkafka)

add_executable(psim_pg_test_worker tests/worker.cpp)
psim_target_defaults(psim_pg_test_worker)
target_link_libraries(psim_pg_test_worker PRIVATE psim::platform_pg GTest::gtest)
target_compile_definitions(psim_platform_pg_component_test PRIVATE PSIM_PG_TEST_WORKER="$<TARGET_FILE:psim_pg_test_worker>")
add_dependencies(psim_platform_pg_component_test psim_pg_test_worker)

add_executable(psim_pg_throughput benchmarks/throughput.cpp)
psim_target_defaults(psim_pg_throughput)
target_link_libraries(psim_pg_throughput PRIVATE psim::platform_pg)
