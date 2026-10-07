cmake_minimum_required(VERSION 3.28)

project(psim
  VERSION 0.1.0
  DESCRIPTION "PSIM Platform"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)  # C++20 modules are not used
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Dependencies are built in Release (conanfile.py); map every project configuration to it.
# The empty element also allows configuration-less imported targets (protoc, grpc_cpp_plugin).
foreach(config Debug RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" upper)
  set(CMAKE_MAP_IMPORTED_CONFIG_${upper} "Release;")
endforeach()

include(PsimOptions)
include(PsimCompiler)
include(PsimProto)
include(PsimPackaging)
include(PsimService)

enable_testing()
find_package(GTest REQUIRED CONFIG)
find_package(benchmark REQUIRED CONFIG)

add_subdirectory(contracts)
add_subdirectory(libs)
add_subdirectory(tools/psimctl)
add_subdirectory(tools/service-template)
add_subdirectory(services)

psim_finalize_packaging()
