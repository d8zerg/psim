// psimctl - PSIM administration CLI and operational command dispatcher.

#include <google/protobuf/util/json_util.h>
#include <unistd.h>

#include <cstdio>
#include <exception>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "operations.hpp"
#include "psim/common/v1/envelope.pb.h"
#include "psim/platform/build_info.hpp"

namespace {

int print_version() {
  const auto info = psim::platform::build_info();
  std::println("psimctl {} ({}, {}, {})", info.version, info.git_commit, info.compiler, info.build_type);
  return 0;
}

int print_envelope_sample() {
  psim::common::v1::Envelope envelope;
  envelope.set_message_id("01928f5e-7a3b-7c4d-8e9f-0a1b2c3d4e5f");
  envelope.set_tenant_id("01928f5e-0000-7000-8000-000000000001");
  envelope.set_correlation_id(envelope.message_id());
  envelope.mutable_actor()->set_kind(psim::common::v1::ACTOR_KIND_SYSTEM);
  envelope.mutable_actor()->set_id("psimctl");

  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
  std::string json;
  if (!google::protobuf::util::MessageToJsonString(envelope, &json, options).ok()) {
    std::println(stderr, "psimctl: cannot serialize the envelope");
    return 1;
  }
  std::print("{}", json);
  return 0;
}

int usage() {
  std::println(stderr,
               "usage: psimctl version | contracts envelope-sample | gateway | migrate postgres|clickhouse | "
               "audit | projection console|facts | [--config FILE] diagnose|backup|plans|dlq|history|normalizer|"
               "certificates|replay|support");
  return 2;
}

int execute(std::string binary, std::vector<std::string> values) {
  std::vector<char*> arguments;
  arguments.reserve(values.size() + 2);
  arguments.push_back(binary.data());
  for (auto& value : values) {
    arguments.push_back(value.data());
  }
  arguments.push_back(nullptr);
  // Replace the launcher, preserving signals, exit status and file descriptors without a shell.
  (void)::execv(binary.c_str(), arguments.data());
  std::println(stderr, "psimctl: operational runtime or tool is unavailable");
  return 1;
}

int run(std::span<char*> args) {
  if (args.size() == 2 && std::string_view{args[1]} == "version") {
    return print_version();
  }
  if (args.size() == 3 && std::string_view{args[1]} == "contracts" && std::string_view{args[2]} == "envelope-sample") {
    return print_envelope_sample();
  }
  if (args.size() < 2) {
    return usage();
  }
  const std::string_view command(args[1]);
  std::string tool;
  std::size_t skip = 2;
  if (command == "gateway") {
    tool = "psim-gateway-admin";
  } else if (command == "audit") {
    tool = "psim-audit-tool";
  } else if (command == "migrate" && args.size() >= 3) {
    const std::string_view target(args[2]);
    if (target != "postgres" && target != "clickhouse") {
      return usage();
    }
    tool = target == "postgres" ? "psim-pg-migrate" : "psim-ch-migrate";
    skip = 3;
  } else if (command == "projection" && args.size() >= 3) {
    const std::string_view target(args[2]);
    if (target != "console" && target != "facts") {
      return usage();
    }
    tool = target == "console" ? "psim-console-tool" : "psim-report-tool";
    skip = 3;
  }
  std::vector<std::string> values;
  if (!tool.empty()) {
    for (const auto* argument : args.subspan(skip)) {
      values.emplace_back(argument);
    }
    return execute("/usr/bin/" + tool, std::move(values));
  }
  if (command != "--config" && command != "--help" && command != "diagnose" && command != "backup" && command != "plans"
      && command != "dlq" && command != "history" && command != "normalizer" && command != "certificates"
      && command != "replay" && command != "support") {
    return usage();
  }
  return psim::ctl::operations(args.subspan(1));
}

}  // namespace

int main(int argc, char** argv) {
  // Exceptions of the standard library and protobuf stop here, with a message instead of std::terminate.
  try {
    return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception& e) {
    // Non-throwing output; nothing is left to do if stderr itself fails.
    (void)std::fputs("psimctl: ", stderr);
    (void)std::fputs(e.what(), stderr);
    (void)std::fputs("\n", stderr);
    return 1;
  }
}
