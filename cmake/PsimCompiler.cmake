# Compiler and linker settings of PSIM targets (ADR-032).
#
# psim_target_defaults(<target>) applies to project code: strict warnings, hardening,
# sanitizers, coverage. Generated and third-party code gets only the common flags below.

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "PSIM requires clang (toolchain: tools/toolchain/Dockerfile), got ${CMAKE_CXX_COMPILER_ID}")
endif()

# --- Common flags for all code -------------------------------------------------------------
# Reproducible builds: no absolute paths in debug info and macros, stable build id, no dates.
# Coverage builds keep absolute paths: llvm-cov resolves sources through them (ADR-033).
if(NOT PSIM_COVERAGE)
  add_compile_options(
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=.")
endif()
add_compile_options(-fno-record-gcc-switches)
add_link_options(-fuse-ld=lld LINKER:--build-id=sha1)

# Hardening (all configurations): stack protection, control-flow protection, full RELRO, PIE.
add_compile_options(-fstack-protector-strong -fstack-clash-protection -fcf-protection=full)
add_link_options(LINKER:-z,relro LINKER:-z,now LINKER:-z,noexecstack -pie)
# libc++ hardening: extensive checks in debug builds, fast checks otherwise.
add_compile_definitions(
  "_LIBCPP_HARDENING_MODE=$<IF:$<CONFIG:Debug>,_LIBCPP_HARDENING_MODE_DEBUG,_LIBCPP_HARDENING_MODE_FAST>")
add_compile_definitions("$<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=3>")

if(PSIM_STATIC_RUNTIME)
  # Executables do not depend on libc++ being installed on Debian or Astra Linux.
  add_link_options("$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:-static-libstdc++>")
endif()

# --- Project code ---------------------------------------------------------------------------
set(PSIM_WARNINGS
  -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Wcast-align -Wcast-qual
  -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
  -Wformat=2 -Wimplicit-fallthrough -Wmissing-declarations -Wundef
  -Wdate-time)  # __DATE__/__TIME__ break reproducibility

function(psim_target_defaults target)
  target_compile_options(${target} PRIVATE ${PSIM_WARNINGS})
  if(PSIM_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
  if(PSIM_SANITIZE)
    list(JOIN PSIM_SANITIZE "," sanitizers)
    target_compile_options(${target} PRIVATE -fsanitize=${sanitizers} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(${target} PRIVATE -fsanitize=${sanitizers})
  endif()
  if(PSIM_COVERAGE)
    target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(${target} PRIVATE -fprofile-instr-generate)
  endif()
endfunction()

# psim_add_test(<name> SOURCES ... LIBS ...): GoogleTest executable registered in CTest.
function(psim_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  psim_target_defaults(${name})
  target_link_libraries(${name} PRIVATE ${ARG_LIBS} GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)
endfunction()
