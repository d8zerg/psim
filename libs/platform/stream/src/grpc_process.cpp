#include "psim/platform/stream/grpc_process.hpp"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

#include <stdexcept>

namespace psim::platform::stream {
GrpcProcess::GrpcProcess() {
  // Own OpenSSL initialization before gRPC can request OPENSSL_INIT_NO_ATEXIT.
  if (OPENSSL_init_ssl(0, nullptr) != 1) {
    throw std::runtime_error("OpenSSL initialization failed");
  }
  grpc_init();
  grpc_event_engine::experimental::SetDefaultEventEngine(grpc_event_engine::experimental::CreateEventEngine());
}

GrpcProcess::~GrpcProcess() {
  grpc_shutdown();
  // Join workers before cleaning their OpenSSL thread-local random generators.
  grpc_event_engine::experimental::ShutdownDefaultEventEngine();
  OPENSSL_cleanup();
}
}  // namespace psim::platform::stream
