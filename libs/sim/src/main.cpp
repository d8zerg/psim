#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "psim/sim/pipeline.hpp"

namespace {

struct Arguments {
  psim::sim::Options options;
  std::uint64_t runs = 200;
  std::filesystem::path trace_dir = ".cache/sim";
  bool save_success = false;
};

std::uint64_t number(std::string_view value) {
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(value.begin(), value.end(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.end() || value.empty()) {
    throw std::invalid_argument("expected an unsigned decimal integer");
  }
  return parsed;
}

Arguments parse(std::span<char*> args) {
  Arguments parsed;
  for (std::size_t index = 1; index < args.size(); ++index) {
    const std::string_view name(args[index]);
    if (name == "--save-trace") {
      parsed.save_success = true;
      continue;
    }
    if (++index == args.size()) {
      throw std::invalid_argument("option requires a value");
    }
    const std::string_view value(args[index]);
    if (name == "--trace-dir") {
      parsed.trace_dir = value;
      continue;
    }
    const auto integer = number(value);
    if (name == "--seed") {
      parsed.options.seed = integer;
    } else if (name == "--runs") {
      parsed.runs = integer;
    } else if (name == "--events") {
      parsed.options.events = static_cast<std::size_t>(integer);
    } else if (name == "--partitions") {
      parsed.options.partitions = static_cast<std::size_t>(integer);
    } else if (name == "--fault-decisions") {
      parsed.options.fault_decisions = static_cast<std::size_t>(integer);
    } else if (name == "--max-handlers") {
      parsed.options.budget.handlers = static_cast<std::size_t>(integer);
    } else if (name == "--max-time-ms" && integer <= 1000000000) {
      parsed.options.budget.virtual_time = std::chrono::milliseconds{integer};
    } else if (name == "--model-version" && integer == psim::sim::kModelVersion) {
      // Replay refuses to silently use a different model version.
    } else {
      throw std::invalid_argument("unknown option or unsupported value/model version");
    }
  }
  if (parsed.runs == 0 || parsed.runs > 100000
      || parsed.runs - 1 > std::numeric_limits<std::uint64_t>::max() - parsed.options.seed || parsed.options.events == 0
      || parsed.options.events > 10000 || parsed.options.partitions == 0 || parsed.options.partitions > 64
      || parsed.options.budget.handlers == 0) {
    throw std::invalid_argument("invalid range of seeds, runs, events, partitions or handlers");
  }
  return parsed;
}

void save(const psim::sim::Report& report, const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  const auto path =
      directory
      / ("v" + std::to_string(psim::sim::kModelVersion) + "-seed-" + std::to_string(report.options.seed) + ".trace");
  std::ofstream file(path);
  file.exceptions(std::ios::failbit | std::ios::badbit);
  file << "model=" << psim::sim::kModelVersion << " seed=" << report.options.seed << " events=" << report.options.events
       << " partitions=" << report.options.partitions << " fault_decisions=" << report.options.fault_decisions
       << " max_handlers=" << report.options.budget.handlers << " max_time_ms="
       << std::chrono::duration_cast<std::chrono::milliseconds>(report.options.budget.virtual_time).count()
       << "\nok=" << report.ok << " fingerprint=" << report.fingerprint << " error=" << report.error << '\n';
  for (const auto& entry : report.trace) {
    file << entry << '\n';
  }
  file.close();
  std::cout << "trace=" << path.string() << '\n';
}

int run(const Arguments& args) {
  std::uint64_t fingerprint = 0;
  std::size_t restarts = 0;
  std::size_t publications = 0;
  for (std::uint64_t index = 0; index < args.runs; ++index) {
    auto options = args.options;
    options.seed += index;
    const auto report = psim::sim::run_pipeline(options);
    if (!report.ok) {
      std::cerr << "FAIL seed=" << options.seed << " model=" << psim::sim::kModelVersion << ": " << report.error << '\n'
                << "replay: task sim:replay SEED=" << options.seed << " MODEL_VERSION=" << psim::sim::kModelVersion
                << " EVENTS=" << options.events << " PARTITIONS=" << options.partitions
                << " FAULT_DECISIONS=" << options.fault_decisions << " MAX_HANDLERS=" << options.budget.handlers
                << " MAX_TIME_MS="
                << std::chrono::duration_cast<std::chrono::milliseconds>(options.budget.virtual_time).count() << '\n';
      save(report, args.trace_dir);
      return 1;
    }
    if (args.save_success) {
      save(report, args.trace_dir);
    }
    fingerprint = (fingerprint * 1099511628211ULL) ^ report.fingerprint;
    restarts += report.restarts;
    publications += report.publications;
    if ((index + 1) % 1000 == 0) {
      std::cout << "progress=" << index + 1 << '/' << args.runs << '\n';
    }
  }
  std::cout << "PASS model=" << psim::sim::kModelVersion << " seed=" << args.options.seed << " runs=" << args.runs
            << " events=" << args.options.events << " restarts=" << restarts << " publications=" << publications
            << " fingerprint=" << fingerprint << '\n';
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() == 2 && std::string_view(args[1]) == "--help") {
      std::cout
          << "psim-sim [--seed N] [--runs N] [--events N] [--partitions N] [--fault-decisions N]\n"
             "         [--max-handlers N] [--max-time-ms N] [--model-version 1] [--trace-dir PATH] [--save-trace]\n"
             "Exit: 0=all oracles passed, 1=simulation failed, 2=arguments or trace I/O failed.\n";
      return 0;
    }
    return run(parse(args));
  } catch (const std::exception& error) {
    std::cerr << "psim-sim: " << error.what() << '\n';
    return 2;
  }
}
