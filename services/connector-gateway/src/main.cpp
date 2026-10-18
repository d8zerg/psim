#include <memory>

#include "gateway.hpp"
#include "psim/platform/connector/grpc_process.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/runtime.hpp"

int main(int argc, char** argv) {
  const psim::platform::connector::GrpcProcess grpc;
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "connector-gateway",
                                       .settings_schema = psim::connector_gateway::settings_schema(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         result.push_back(std::make_unique<psim::connector_gateway::Gateway>(context));
                                         return result;
                                       }});
}
