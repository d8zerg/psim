# Platform trust primitives (ADR-043); cryptography stays in OpenSSL 3.
find_package(Boost REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
add_library(psim_platform_security STATIC src/secret.cpp src/tls.cpp src/http.cpp src/jwt.cpp src/introspection.cpp src/trust.cpp)
add_library(psim::platform_security ALIAS psim_platform_security)
target_include_directories(psim_platform_security PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_security PUBLIC psim::platform_async psim::platform_errors Boost::headers OpenSSL::SSL
  PRIVATE OpenSSL::Crypto nlohmann_json::nlohmann_json)
psim_target_defaults(psim_platform_security)
psim_add_test(psim_platform_security_test SOURCES tests/security_test.cpp LIBS psim::platform_security nlohmann_json::nlohmann_json)
psim_add_component_test(psim_platform_security_component_test SOURCES tests/component_test.cpp
  LIBS psim::platform_security psim::platform_kafka psim::platform_pg psim::platform_redis nlohmann_json::nlohmann_json)
