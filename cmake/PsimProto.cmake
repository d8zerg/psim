# C++ code generation from Protobuf and gRPC contracts.
#
# psim_proto_library(<target>
#   IMPORT_DIRS <dir>...     proto roots (-I); generated files keep paths relative to them
#   PROTOS <file>...         messages to generate C++ for
#   GRPC_PROTOS <file>...    files whose services get gRPC C++ stubs)
#
# protoc and grpc_cpp_plugin come from the Conan build context, so generated code always matches
# the linked runtime versions. Generated code is compiled without project warnings.

find_package(protobuf REQUIRED CONFIG)
find_package(gRPC REQUIRED CONFIG)

function(psim_proto_library target)
  cmake_parse_arguments(ARG "" "" "IMPORT_DIRS;PROTOS;GRPC_PROTOS" ${ARGN})
  set(out "${CMAKE_CURRENT_BINARY_DIR}/gen")
  file(MAKE_DIRECTORY "${out}")

  add_library(${target} STATIC)
  protobuf_generate(
    TARGET ${target}
    LANGUAGE cpp
    IMPORT_DIRS ${ARG_IMPORT_DIRS}
    PROTOC_OUT_DIR "${out}"
    PROTOS ${ARG_PROTOS})
  if(ARG_GRPC_PROTOS)
    protobuf_generate(
      TARGET ${target}
      LANGUAGE grpc
      GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
      PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
      IMPORT_DIRS ${ARG_IMPORT_DIRS}
      PROTOC_OUT_DIR "${out}"
      PROTOS ${ARG_GRPC_PROTOS})
  endif()

  target_include_directories(${target} SYSTEM PUBLIC "$<BUILD_INTERFACE:${out}>")
  target_link_libraries(${target} PUBLIC protobuf::libprotobuf gRPC::grpc++)
  target_compile_options(${target} PRIVATE -w)
endfunction()
