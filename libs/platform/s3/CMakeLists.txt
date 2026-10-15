# S3 adapter: AWS SDK, bounded file streams and conditional immutable writes.
find_package(AWSSDK REQUIRED CONFIG)
find_package(CURL REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)
find_package(prometheus-cpp REQUIRED CONFIG)
# AWS CRT needs libatomic; include the GCC archive so the Debian runtime needs no extra shared library.
find_library(PSIM_S3_ATOMIC NAMES libatomic.a PATHS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES} REQUIRED)
add_library(psim_platform_s3 STATIC src/client.cpp src/files.cpp src/transport.cpp src/metrics.cpp)
add_library(psim::platform_s3 ALIAS psim_platform_s3)
target_include_directories(psim_platform_s3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(psim_platform_s3 PUBLIC psim::platform_async psim::platform_errors psim::platform_security prometheus-cpp::core
  PRIVATE AWS::aws-sdk-cpp-s3 CURL::libcurl OpenSSL::SSL OpenSSL::Crypto
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,${PSIM_S3_ATOMIC}>")
# Conan also names shared atomic transitively; drop that unused dependency after loading the archive.
target_link_options(psim_platform_s3 INTERFACE "LINKER:--as-needed")
psim_target_defaults(psim_platform_s3)
psim_add_test(psim_platform_s3_test SOURCES tests/value_test.cpp LIBS psim::platform_s3 AWS::aws-sdk-cpp-s3 OpenSSL::Crypto)
psim_add_component_test(psim_platform_s3_component_test SOURCES tests/component_test.cpp tests/network_test.cpp LIBS psim::platform_s3 AWS::aws-sdk-cpp-s3)
