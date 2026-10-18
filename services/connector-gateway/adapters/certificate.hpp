#pragma once

#include <grpcpp/security/auth_context.h>

#include <string_view>

#include "protocol.hpp"
#include "psim/platform/error.hpp"

namespace psim::connector_gateway {
// Trust is checked against the configured CA, including for public certificates submitted by operators.
[[nodiscard]] platform::Result<Certificate> certificate(std::string_view pem, std::string_view ca_file);
[[nodiscard]] platform::Result<Certificate> certificate(const grpc::AuthContext& context, std::string_view ca_file);
}  // namespace psim::connector_gateway
