# Valkey adapter (step 2.5, ADR-015): Boost.Redis RESP3, Sentinel and ephemeral data patterns.
find_package(Boost REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(protobuf REQUIRED CONFIG)
add_library(psim_platform_redis STATIC src/boost_redis.cpp src/connection.cpp src/pool.cpp src/serde.cpp src/patterns.cpp)
add_library(psim::platform_redis ALIAS psim_platform_redis)
target_include_directories(psim_platform_redis PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_redis PUBLIC psim::platform_async psim::platform_errors protobuf::libprotobuf
  PRIVATE psim::platform_security OpenSSL::SSL OpenSSL::Crypto nlohmann_json::nlohmann_json)
psim_target_defaults(psim_platform_redis)
psim_add_test(psim_platform_redis_test SOURCES tests/value_test.cpp LIBS psim::platform_redis)
psim_add_component_test(psim_platform_redis_component_test SOURCES tests/component_test.cpp LIBS psim::platform_redis OpenSSL::SSL)
