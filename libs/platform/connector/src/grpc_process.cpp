#include "psim/platform/connector/grpc_process.hpp"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

#include <stdexcept>

namespace psim::platform::connector {
GrpcProcess::GrpcProcess() {
  // Match gRPC: the default crypto context has process lifetime, independent of detached worker TLS cleanup.
  if (OPENSSL_init_ssl(OPENSSL_INIT_NO_ATEXIT, nullptr) != 1) {
    throw std::runtime_error("OpenSSL initialization failed");
  }
  grpc_init();
  grpc_event_engine::experimental::SetDefaultEventEngine(grpc_event_engine::experimental::CreateEventEngine());
}

GrpcProcess::~GrpcProcess() {
  grpc_shutdown();
  // Drain transport work; do not unload process-wide crypto state from a library destructor.
  grpc_event_engine::experimental::ShutdownDefaultEventEngine();
}
}  // namespace psim::platform::connector
