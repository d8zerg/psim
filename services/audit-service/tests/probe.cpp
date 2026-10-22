#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <print>
#include <span>
#include <string>

#include "chain.hpp"

namespace {
int run(std::span<char*> args) {
  if (args.size() != 2) {
    return 2;
  }
  namespace a = psim::audit_service;
  std::ifstream input(args[1]);
  auto values = a::Json::parse(input);
  std::string previous(32, '\0');
  for (auto& row : values) {
    auto value = a::parse<a::Entry>(row.at("body"));
    if (value.audit_seq() == 1) {
      value.mutable_entry()->set_action("forged.rewrite");
    }
    value.set_prev_hash(previous);
    value.set_hash(a::entry_hash(value));
    previous = value.hash();
    row["body"] = a::json(value);
  }
  std::println("{}", values.dump());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception&) {
    (void)std::fputs("invalid rewrite fixture\n", stderr);
    return 1;
  }
}
