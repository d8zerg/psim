# psim::platform_kafka - Kafka layer (step 2.3, ADR-041): librdkafka on Asio without polling
# threads, idempotent and transactional producer, group consumer with manual offsets, the
# consume - handle - produce processor (ALO and EOS-K, retries, DLQ), Confluent wire format with
# the Schema Registry client, W3C trace context in headers, metrics.
find_package(RdKafka REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG)
find_package(protobuf REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(opentelemetry-cpp REQUIRED CONFIG)
find_package(prometheus-cpp REQUIRED CONFIG)

add_library(psim_platform_kafka STATIC
  src/detail.cpp src/message.cpp src/metrics.cpp src/producer.cpp src/consumer.cpp src/processor.cpp
  src/wire_format.cpp src/schema_registry.cpp src/trace_context.cpp)
add_library(psim::platform_kafka ALIAS psim_platform_kafka)
target_include_directories(psim_platform_kafka PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_kafka
  PUBLIC psim::platform_async psim::platform_errors psim::platform_log Boost::headers protobuf::libprotobuf
         opentelemetry-cpp::api prometheus-cpp::core
  PRIVATE RdKafka::rdkafka nlohmann_json::nlohmann_json)
psim_target_defaults(psim_platform_kafka)

psim_add_test(psim_platform_kafka_test
  SOURCES tests/wire_format_test.cpp tests/serde_test.cpp tests/retry_test.cpp tests/client_test.cpp
          tests/schema_registry_test.cpp
  LIBS psim::platform_kafka psim::contracts RdKafka::rdkafka)

# Component tests (TS-03) and their worker process, killed on purpose by the crash tests.
add_executable(psim_kafka_test_worker tests/worker.cpp)
psim_target_defaults(psim_kafka_test_worker)
target_link_libraries(psim_kafka_test_worker PRIVATE psim::platform_kafka)

psim_add_component_test(psim_platform_kafka_component_test
  SOURCES tests/component_test.cpp tests/crash_test.cpp
  LIBS psim::platform_kafka psim::contracts RdKafka::rdkafka opentelemetry-cpp::trace
       opentelemetry-cpp::exporter_in_memory)
target_compile_definitions(psim_platform_kafka_component_test
  PRIVATE PSIM_KAFKA_TEST_WORKER="$<TARGET_FILE:psim_kafka_test_worker>")
add_dependencies(psim_platform_kafka_component_test psim_kafka_test_worker)

psim_add_benchmark(psim_platform_kafka_bench
  SOURCES benchmarks/serde_bench.cpp
  LIBS psim::platform_kafka psim::contracts)

# B-01: throughput against the environment (`task bench:kafka`), not a CTest test.
add_executable(psim_kafka_throughput benchmarks/throughput.cpp)
psim_target_defaults(psim_kafka_throughput)
target_link_libraries(psim_kafka_throughput PRIVATE psim::platform_kafka RdKafka::rdkafka)
