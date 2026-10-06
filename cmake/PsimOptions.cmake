# Build options of the PSIM project; set by CMakePresets.json.

option(PSIM_WERROR "Treat compiler warnings as errors" ON)
set(PSIM_SANITIZE "" CACHE STRING "Sanitizers: address;undefined or thread (empty: none)")
option(PSIM_COVERAGE "Instrument for llvm-cov source-based coverage" OFF)
option(PSIM_STATIC_RUNTIME "Link libc++ and libc++abi statically into executables" ON)

if("thread" IN_LIST PSIM_SANITIZE AND "address" IN_LIST PSIM_SANITIZE)
  message(FATAL_ERROR "PSIM_SANITIZE: thread and address sanitizers cannot be combined")
endif()
