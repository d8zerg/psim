find_package(yaml-cpp REQUIRED CONFIG)
find_package(RocksDB REQUIRED CONFIG)
add_library(psim_correlation_rules STATIC application/validation.cpp application/replay.cpp adapters/repository.cpp adapters/jobs.cpp adapters/history.cpp adapters/http.cpp adapters/management.cpp application/live.cpp adapters/state_store.cpp adapters/live_worker.cpp)
psim_target_defaults(psim_correlation_rules)
target_include_directories(psim_correlation_rules PUBLIC application adapters)
target_link_libraries(psim_correlation_rules PUBLIC psim::platform_authz psim::domain_processing psim::platform_stream psim::platform_clickhouse yaml-cpp::yaml-cpp RocksDB::rocksdb)
psim_add_test(psim_rules_test SOURCES tests/expression_test.cpp tests/validation_test.cpp tests/replay_test.cpp tests/authorization_test.cpp tests/live_test.cpp tests/state_store_test.cpp LIBS psim_correlation_rules)
psim_add_service(correlation-engine SOURCES src/main.cpp LIBS psim_correlation_rules SUMMARY "PSIM correlation-engine")

install(FILES ${PROJECT_SOURCE_DIR}/contracts/taxonomy/v1/taxonomy.yaml DESTINATION share/psim RENAME correlation-taxonomy.yaml COMPONENT correlation-engine)

option(PSIM_RULE_FUZZING "Build the instrumented rule parser and validator" OFF)
if(PSIM_RULE_FUZZING)
  if(NOT "address" IN_LIST PSIM_SANITIZE OR NOT "undefined" IN_LIST PSIM_SANITIZE)
    message(FATAL_ERROR "Rule fuzzing requires address and undefined sanitizers")
  endif()
  add_library(psim_rules_fuzz_core STATIC EXCLUDE_FROM_ALL
    ../../libs/domain/processing/src/expression.cpp
    application/validation.cpp)
  psim_target_defaults(psim_rules_fuzz_core)
  target_compile_options(psim_rules_fuzz_core PRIVATE -fsanitize=fuzzer-no-link)
  target_link_libraries(psim_rules_fuzz_core PUBLIC psim_correlation_rules)
  add_executable(psim-rules-fuzz EXCLUDE_FROM_ALL tests/fuzz.cpp)
  psim_target_defaults(psim-rules-fuzz)
  target_compile_options(psim-rules-fuzz PRIVATE -fsanitize=fuzzer-no-link)
  target_link_options(psim-rules-fuzz PRIVATE -fsanitize=fuzzer)
  target_link_libraries(psim-rules-fuzz PRIVATE psim_rules_fuzz_core)
endif()

add_executable(psim_correlation_sim EXCLUDE_FROM_ALL tests/simulation_test.cpp)
psim_target_defaults(psim_correlation_sim)
target_link_libraries(psim_correlation_sim PRIVATE psim_correlation_rules psim::sim GTest::gtest_main)

psim_add_benchmark(psim_correlation_bench SOURCES benchmarks/rules_bench.cpp LIBS psim_correlation_rules)
