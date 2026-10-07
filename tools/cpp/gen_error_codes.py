#!/usr/bin/env python3
"""Generate the C++ error code header from the error catalog (step 2.1, ADR-039).

  gen_error_codes.py <contracts/errors/errors.yaml> <out/psim/platform/error_codes.hpp>

Every catalog code becomes an ErrorCode enumerator (COMMON_NOT_FOUND -> kCommonNotFound) and a row of
kErrorCodes with its metadata, so services use exactly the codes of the contract (REST Problem.code,
connector protocol, DLQ, command results, realtime). Run by CMake at build time; not committed.
"""

import sys

import yaml


def enumerator(code):
    return "k" + "".join(part.capitalize() for part in code.lower().split("_"))


def literal(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[1], encoding="utf-8") as f:
        codes = yaml.safe_load(f)["codes"]
    out = [
        "// Generated from contracts/errors/errors.yaml by tools/cpp/gen_error_codes.py. Do not edit.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace psim::platform {",
        "",
        "enum class ErrorCode : std::uint16_t {",
        *[f"  {enumerator(c['code'])}," for c in codes],
        "};",
        "",
        "struct ErrorCodeInfo {",
        "  ErrorCode code;",
        "  std::string_view name;",
        "  int http_status;  // 0: not used in REST",
        "  std::string_view grpc_status;",
        "  bool retryable;",
        "  std::string_view title;",
        "};",
        "",
        f"inline constexpr std::array<ErrorCodeInfo, {len(codes)}> kErrorCodes{{{{",
        *[f"    {{ErrorCode::{enumerator(c['code'])}, {literal(c['code'])}, {c.get('http') or 0}, "
          f"{literal(c.get('grpc') or '')}, {'true' if c.get('retryable') else 'false'}, {literal(c['title'])}}},"
          for c in codes],
        "}};",
        "",
        "}  // namespace psim::platform",
        "",
    ]
    with open(argv[2], "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
