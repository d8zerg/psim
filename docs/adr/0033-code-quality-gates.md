# ADR-033. Code quality gates: formatting, clang-tidy, includes, sanitizers, coverage and fitness functions

<!-- language-links -->
[English](0033-code-quality-gates.md) | [Русский](../ru/adr/0033-code-quality-gates.md)
<!-- /language-links -->

- **Status:** Accepted
- **Refinement:** [ADR-036](0036-no-git-hooks.md) replaces the Git-hook rules: run `task ci` before committing; `hooks:*` tasks have been removed.
- **Related:** ADR-001, ADR-030, ADR-031, ADR-032, [C++ standard](../engineering/cpp-coding-standard.md), [TypeScript standard](../engineering/typescript-coding-standard.md), requirement SR-13 ([threat-register.md](../security/threat-register.md))

## Context

Coding-standard violations, incorrect dependency directions and insufficient coverage block merging. Engineering goals G-11-G-14 require zero compiler and clang-tidy warnings, no ASan, UBSan or TSan findings, at least 90% line and 80% branch coverage in the domain core, and at least 75% line coverage in infrastructure. Registry methods FF-01, FF-02, FF-07, FF-10, FF-11, FF-12 and SEC-01 verify these requirements.

Constraints:

- The toolchain is Clang 21 with system libstdc++ in a Debian 13 x86_64 container (ADR-032); analysis tools must understand the same code and standard-library headers.
- Ordinary Conan dependencies are not instrumented; the `tsan` preset's protobuf headers require a TSan-built library (ADR-032).
- The merge gate is `task check`, invoked unchanged by CI.
- Architecture-check self-tests must demonstrate detection with negative examples.

## Options considered

### Include checks

| Option | Advantages | Disadvantages |
|---|---|---|
| include-what-you-use | Mature reports | IWYU releases depend on Clang versions; Clang 21 requires a source build against `llvm-21-dev` in the toolchain image, a separate pass and suppression format |
| **clang-tidy `misc-include-cleaner`** | Same LLVM include-cleaner analysis as clangd; included in `clang-tidy-21`; one pass, `NOLINT` format and `WarningsAsErrors` gate; immediate editor diagnostics | Does not suggest forward declarations; third-party header mappings use regular expressions |

### Thread sanitizer

| Option | Advantages | Disadvantages |
|---|---|---|
| TSan suppressions for protobuf and gRPC | No dependency rebuild | Can hide real application races through these libraries; do not remove false `heap-use-after-free` reports from `TSanRead` |
| **Separate Conan profile with `-fsanitize=thread` for dependencies** | Entire process is instrumented; reliable reports | A second cached dependency build; flags must affect package identity to prevent mixing instrumented and ordinary binaries |

### Coverage gate

| Option | Advantages | Disadvantages |
|---|---|---|
| One project-wide percentage | Simple | Well-tested modules conceal untested ones |
| **Per-module directory thresholds with `domain` and `infrastructure` groups** | Quality thresholds apply to each library | Group configuration in `tools/cpp/coverage.yaml` |

### Dependency fitness function

| Option | Advantages | Disadvantages |
|---|---|---|
| Parse `CMakeLists.txt` with regular expressions | No build required | Fragile with variables, functions and generator expressions |
| `cmake --graphviz` | Built into CMake | Human-oriented format without target directories or includes |
| **CMake File API code model v2 plus `#include` analysis** | Accurate configured target graph with directories, sources and include paths; include analysis finds header dependencies absent from the target graph | Requires build configuration, taking seconds with prepared dependencies |

## Decision

1. **Coding standards:** [cpp-coding-standard.md](../engineering/cpp-coding-standard.md) and [typescript-coding-standard.md](../engineering/typescript-coding-standard.md). Tool configurations define automated rules; the documents explain them and review-only rules.
2. **Formatting:** `.clang-format`, based on Google style with 120 columns and include grouping, plus `.editorconfig`; `task cpp:format` checks and `task cpp:format:fix` fixes.
3. **clang-tidy:** `.clang-tidy` enables `bugprone`, `cert`, `clang-analyzer`, `concurrency`, `cppcoreguidelines`, `misc`, `modernize`, `performance`, `portability` and `readability`, with explicit exclusions and naming rules; `WarningsAsErrors: '*'`. `task cpp:lint` checks all project translation units using the `debug` build's `compile_commands.json`, excluding generated and vendored code. Suppressions require `NOLINT(<check>)` and a reason.
4. **Includes:** use `misc-include-cleaner` within clang-tidy instead of separate include-what-you-use.
5. **Sanitizers:** `asan` combines ASan and UBSan with ordinary dependencies. `tsan` uses `tools/conan/profiles/clang21-libstdcxx-tsan` dependencies in `build/debian13/conan-tsan`. Sanitizer flags affect package identity through `tools.info.package_id:confs`; protoc and `grpc_cpp_plugin` build tools use the ordinary profile. `task cpp:test:sanitizers` runs both presets; any finding fails the test through `-fno-sanitize-recover=all`.
6. **Coverage:** llvm-cov source-based coverage uses the `coverage` preset. `task cpp:coverage` merges profiles, writes text and HTML reports, and enforces module thresholds: `libs/domain/*` needs 90% lines and 80% branches; `libs/platform/*` and `services/*/adapters` need 75% lines. Tests, generated code and dependencies are excluded.
7. **Fitness functions:** implementation in `tools/arch/arch_check.py`, rules in `tools/arch/architecture.yaml`:
   - `task arch:deps` (FF-01) checks the CMake File API target graph and `#include` directives against ADR-001: domain code depends only on `libs/domain/common` and the standard library, with no external include directories; domain contexts do not depend on each other; services do not depend on other services and use only their own domain contexts; tests may additionally use `libs/sim`.
   - `task arch:domain-purity` (FF-02) prohibits system clocks, randomness, I/O, threads and exceptions in `libs/domain`, checking headers and code constructs while excluding comments and strings.
   - `task arch:sql` (FF-07, SR-13) prohibits concatenating uppercase-keyword SQL literals with runtime data or passing them to formatting. Justified exceptions require `psim-arch: allow-sql(<reason>)`. This is a heuristic; the PostgreSQL query API provides the strict guarantee by accepting only compile-time-known text.
   - `task arch:selftest` verifies that each rule detects its own violation.
8. **Git hooks:** the original `tools/git-hooks/pre-commit`, installed by `task hooks:install`, checked staged-file whitespace and conflict markers, C++ and proto formatting, domain purity, SQL, documentation typography and links. The refinement above supersedes this mechanism.
9. **Merge gate:** `task check` runs contracts, `arch:check`, `cpp:format`, `cpp:lint`, `cpp:test`, `cpp:test:sanitizers`, `cpp:coverage`, `cpp:runtime-test` and documentation. In the original hook workflow, fast hooks did not replace the gate, and `git commit --no-verify` did not bypass CI.
10. **TypeScript:** linting, formatting, type checks and Vitest coverage are included in `task web:check` and `task check`.

## Consequences

- Formatting violations, clang-tidy diagnostics, invalid module dependencies, impure domain code, constructed SQL, sanitizer findings or insufficient coverage fail `task check`.
- The first `tsan` run rebuilds dependencies, taking tens of minutes; later runs use Conan cache.
- clang-tidy checks only code built by `debug`; conditionally compiled code for other platforms is not checked, and no other platforms are supported.
- A new service must declare its domain contexts in `tools/arch/architecture.yaml` or `arch:deps` fails.
- New coverage groups, such as Connector SDK, are added to `tools/cpp/coverage.yaml` with the quality-strategy threshold.

## Verification

Run `task check` and rule self-tests through `task arch:selftest`. Negative checks introduce formatting errors, clang-tidy diagnostics, a TSan data race and insufficient coverage; each must fail its task.
