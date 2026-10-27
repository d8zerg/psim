#include <cstddef>
#include <cstdio>
#include <exception>
#include <print>
#include <span>
#include <string_view>

#include "monitor.hpp"
#include "psim/platform/build_info.hpp"

int main(int argc, char** argv) {
  try {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() == 2 && std::string_view(arguments[1]) == "--version") {
      std::println("psim-monitoring-agent {}", psim::platform::build_info().version);
      return 0;
    }
    if (arguments.size() == 2 && std::string_view(arguments[1]) == "--help") {
      std::println("usage: psim-monitoring-agent --config FILE | --version");
      return 0;
    }
    return psim::monitoring::run(arguments.subspan(1));
  } catch (const std::exception&) {
    (void)std::fputs("monitoring agent failed\n", stderr);
    return 1;
  }
}
