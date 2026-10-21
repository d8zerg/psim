#include <cstddef>
#include <expected>
#include <string>
#include <utility>

#include "live_worker.hpp"
#include "management.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/runtime.hpp"

int main(int argc, char** argv) {
  return psim::platform::runtime::run(argc, argv,
                                      {.name = "correlation-engine",
                                       .settings_schema = psim::correlation::management::settings(),
                                       .create = [](psim::platform::runtime::Context& context)
                                           -> psim::platform::Result<psim::platform::runtime::Components> {
                                         psim::platform::runtime::Components result;
                                         const auto role = context.config().get<std::string>("settings.role");
                                         if (role == "management") {
                                           auto manager = psim::correlation::management::create(context);
                                           if (!manager) {
                                             return std::unexpected(manager.error());
                                           }
                                           result.push_back(std::move(*manager));
                                         } else {
                                           if (role == "live" || role == "router") {
                                             result.push_back(psim::correlation::live::router(context));
                                           }
                                           if (role == "live" || role == "engine") {
                                             for (std::size_t shard = 0; shard < context.shards().size(); ++shard) {
                                               result.push_back(psim::correlation::live::engine(context, shard));
                                             }
                                           }
                                         }
                                         return result;
                                       }});
}
