# C++ library generated from the contracts (step 1.1): every message of contracts/proto and the
# gRPC stubs of the connector protocol. Third-party imports (google/api, buf/validate) are vendored
# in third_party/proto by `task contracts:vendor`.

file(GLOB_RECURSE psim_protos CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/proto/psim/*.proto")
file(GLOB_RECURSE third_party_protos CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/proto/*.proto")

psim_proto_library(psim_contracts
  IMPORT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/proto"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/proto"
  PROTOS ${third_party_protos} ${psim_protos}
  GRPC_PROTOS "${CMAKE_CURRENT_SOURCE_DIR}/proto/psim/connector/v1/connector.proto")
add_library(psim::contracts ALIAS psim_contracts)

psim_add_test(psim_contracts_test
  SOURCES tests/contracts_test.cpp
  LIBS psim::contracts)

psim_add_benchmark(psim_contracts_bench
  SOURCES benchmarks/envelope_bench.cpp
  LIBS psim::contracts)
