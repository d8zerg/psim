// Entry point of a service process (ADR-039): options, configuration, runtime.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "psim/platform/build_info.hpp"
#include "psim/platform/config.hpp"
#include "psim/platform/log.hpp"
#include "psim/platform/runtime.hpp"

namespace psim::platform::runtime {

namespace {

constexpr int kExitUsage = 2;

int usage(std::string_view service) {
  std::println(stderr, "usage: {} [--config <file>] [--check-config] [--version]", service);
  std::println(stderr, "  settings come from the YAML file and PSIM__<SECTION>__<KEY> environment variables");
  return kExitUsage;
}

}  // namespace

int run(int argc, char** argv, const ServiceDefinition& service) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  std::optional<std::filesystem::path> file;
  bool check_only = false;
  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "--config" && i + 1 < args.size()) {
      file = args[++i];
    } else if (arg == "--check-config") {
      check_only = true;
    } else if (arg == "--version") {
      const auto build = build_info();
      std::println("{} {} ({}, {}, {})", service.name, build.version, build.git_commit, build.compiler,
                   build.build_type);
      return 0;
    } else {
      return usage(service.name);
    }
  }

  auto schema = config::Schema::parse(configuration_schema(service.settings_schema));
  if (!schema) {
    for (const auto& problem : schema.error()) {
      std::println(stderr, "{}: {}", service.name, problem.message);
    }
    return kExitUsage;
  }
  config::Sources sources{.file = file, .environment = config::process_environment()};
  auto loaded = config::load(*schema, sources);
  if (!loaded) {
    // Every problem at once (crosscutting.md section 5), before anything starts.
    for (const auto& problem : loaded.error()) {
      std::println(stderr, "{}: config: {}: {}", service.name, problem.path.empty() ? "<document>" : problem.path,
                   problem.message);
    }
    return kExitUsage;
  }
  if (check_only) {
    std::println("{}: configuration is valid", service.name);
    return 0;
  }
  Runtime runtime(service, std::move(*schema), std::move(*loaded), std::move(sources),
                  std::make_shared<log::StdoutSink>());
  return runtime.run();
}

}  // namespace psim::platform::runtime
