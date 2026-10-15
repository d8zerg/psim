#pragma once

namespace psim::platform::stream {
/// Process-level owner, created on main before any gRPC objects and destroyed after all of them.
class GrpcProcess final {
 public:
  GrpcProcess();
  ~GrpcProcess();
  GrpcProcess(const GrpcProcess&) = delete;
  GrpcProcess& operator=(const GrpcProcess&) = delete;
  GrpcProcess(GrpcProcess&&) = delete;
  GrpcProcess& operator=(GrpcProcess&&) = delete;
};
}  // namespace psim::platform::stream
