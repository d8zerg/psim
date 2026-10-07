// psim-service-template - template of a PSIM service (ADR-039).

#include <memory>
#include <string>

#include "psim/platform/error.hpp"
#include "psim/platform/runtime.hpp"
#include "settings.hpp"
#include "worker.hpp"

int main(int argc, char** argv) {
  using psim::platform::Result;
  using psim::platform::runtime::Components;
  using psim::platform::runtime::Context;

  return psim::platform::runtime::run(
      argc, argv,
      {.name = "service-template",
       .settings_schema = std::string(psim::service_template::kSettingsSchema),
       .create = [](Context& context) -> Result<Components> {
         Components components;
         components.push_back(std::make_unique<psim::service_template::Worker>(context));
         return components;
       }});
}
