// psimctl - PSIM administration CLI. Skeleton of step 1.1: version and a contract sample.

#include <google/protobuf/util/json_util.h>

#include <cstdio>
#include <print>
#include <span>
#include <string>
#include <string_view>

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
  std::println(stderr, "usage: psimctl version | psimctl contracts envelope-sample");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  if (args.size() == 2 && std::string_view{args[1]} == "version") {
    return print_version();
  }
  if (args.size() == 3 && std::string_view{args[1]} == "contracts" && std::string_view{args[2]} == "envelope-sample") {
    return print_envelope_sample();
  }
  return usage();
}
