# C++23 Coding Standards

<!-- language-links -->
[English](cpp-coding-standard.md) | [Русский](../ru/engineering/cpp-coding-standard.md)
<!-- /language-links -->

[ADR-033](../adr/0033-code-quality-gates.md) · toolchain - [ADR-032](../adr/0032-cpp-toolchain.md)

The rules checked by the tool are specified in its configuration: `.clang-format`, `.clang-tidy`, flags `cmake/PsimCompiler.cmake`, `tools/arch/architecture.yaml`. This document explains these rules and adds guidelines for reviews. If the document and configuration differ, the configuration takes precedence, and the document is revised accordingly.

## 1. Quality Gates

| What | How it is checked | Task |
|---|---|---|
| Formatting | clang-format 21, `.clang-format` | `task cpp:format` |
| Compiler warnings | `-Wall -Wextra -Wpedantic -Wconversion ...` and `-Werror` | `task cpp:build` |
| Static analysis, naming, include | clang-tidy 21, `.clang-tidy`, 0 errors | `task cpp:lint` |
| Sanitizers | ASan + UBSan, TSan, 0 failures | `task cpp:test:sanitizers` |
| Coverage | llvm-cov, thresholds `tools/cpp/coverage.yaml` | `task cpp:coverage` |
| Module boundaries | CMake target graph and `#include` (ADR-001) | `task arch:deps` |
| Domain purity | prohibited headers and constructs in `libs/domain` | `task arch:domain-purity` |
| SQL only with parameters | SQL literal heuristic (SR-13) | `task arch:sql` |

All of the above are part of `task check`, which is run in the pipeline `task ci` before each commit ([ADR-034](../adr/0034-local-ci-and-supply-chain.md), [ADR-036](../adr/0036-no-git-hooks.md)). Quick checks for individual items: `task cpp:format`, `task arch:check`, `task docs:typography`.

## 2. Language and Standard Library

- C++23 without compiler extensions, Debian 13 system libstdc++. C++20 modules are not used; headers are protected by `#pragma once`.
- Prefer standard library facilities: `std::expected`, `std::span`, `std::string_view`, `std::format` and `std::print`, `std::ranges`, `std::chrono`.
- Macros are only used where they are necessary (build configuration, conditional compilation); names with a prefix `PSIM_`.
- C arrays, `reinterpret_cast`, `const_cast`, C variadic functions, `goto` are not used (clang-tidy `cppcoreguidelines-*`).

## 3. Formatting

- Base style: Google style: 2-space indentation, up to 120 characters per line, `*` and `&` for types, curly braces for any body `if`, `for`, `while`.
- Include order: groups by blank lines: own header (for `x.cpp` and `x_test.cpp` - `x.hpp`); external `<lib/header.h>`; standard library `<header>`; project-specific `"psim/..."`. clang-format sorts and groups them automatically.
- Formatting is not discussed in reviews: `task cpp:format:fix`.

## 4. Naming

| Entities | Style | Example |
|---|---|---|
| Namespaces | `lower_case` | `psim::domain::incident` |
| Classes, structures, enumerations, type aliases, concepts, template parameters | `CamelCase` | `IncidentId`, `EventPublisher` |
| Functions, methods, variables, parameters, public struct fields | `lower_case` | `open_incident`, `tenant_id` |
| Private and protected fields in classes | `lower_case_` | `state_` |
| Constants (`constexpr`, global and static constants), values of enumerations | `kCamelCase` | `kMaxBatchSize`, `Severity::kCritical` |
| Macros | `PSIM_UPPER_CASE` | `PSIM_VERSION` |
| Files | `lower_case.hpp`, `lower_case.cpp`, test - `lower_case_test.cpp` | `incident_state.hpp` |

Namespaces repeat the library path: `libs/platform/core` -> `psim::platform`, `libs/domain/incident` -> `psim::domain::incident`.

## 5. Module Layout

```
libs/<area>/<name>/
  CMakeLists.txt          target psim_<area>_<name>, alias psim::<area>_<name>
  include/psim/<area>/... public headers (the only external include path)
  src/                    implementation and private headers
  tests/                  <name>_test.cpp, target created through psim_add_test
```

- Every project target uses `psim_target_defaults()` (strict warnings, sanitizers, coverage); tests are created by the function `psim_add_test()`.
- Dependency directions - ADR-001 and `tools/arch/architecture.yaml`. New services are added with a list of their domain contexts.

## 6. Errors

- Domain errors - values: `std::expected<T, Error>`; functions returning errors are marked `[[nodiscard]]`.
- In `libs/domain`, exceptions are not thrown or caught (`arch:domain-purity`).
- In the infrastructure, exceptions from external libraries do not cross the port boundary: the adapter converts them into results.
- At port and API boundaries, an error is `psim::platform::Error` with code from the `contracts/errors/errors.yaml` catalog (`ErrorCode` generated during build) and `Result<T>` = `std::expected<T, Error>`; the message does not contain secrets or personal data ([ADR-039](../adr/0039-service-runtime.md)).
- Move constructors and assignment operations, destructors and `swap` - `noexcept`.

## 7. Resources, Memory, Types

- Ownership - through RAII: `std::unique_ptr` by default, `std::shared_ptr` only when truly shared; `new` and `delete` are not written in application code.
- Parameters for only reading - `std::span`, `std::string_view`, `const T&`; transfer of ownership - by value from `std::move`.
- Identifiers and domain values - separate types (`IncidentId`, not `std::string`); constructors with one argument - `explicit`.
- Type conversions are explicit (`-Wconversion`, `-Wsign-conversion`): `static_cast` or a checking function.
- Enumerations - only `enum class`.

## 8. Deterministic Domain Core

The rule for a clean domain core from [crosscutting.md](../architecture/crosscutting.md#12-testability-and-module-structure): in `libs/domain` there are no system clocks, randomness, input/output, threads and exceptions. Time, identifiers, randomness come through ports (`Clock`, `IdGenerator`, `Random`), publication - through `EventPublisher`. Only the standard library and `libs/domain/common` are allowed. Checks - `arch:domain-purity` and `arch:deps`.

## 9. Concurrency

- Shared mutable state without synchronization is prohibited; thread and coroutine models are defined by ADR-016.
- Thread tests must pass under TSan; TSan suppression is not used - the race is corrected.

## 10. SQL

- Only parameterized queries; query text is a constant known at compile time (SR-13).
- SQL keywords are written in uppercase: `arch:sql` recognizes SQL literals by them.
- If an identifier (e.g., partition name) is actually calculated, it is built from a verified value, and the line is marked `// psim-arch: allow-sql(<reason>)`.

## 11. Suppression and Comments

- Suppression of warnings - only with the name of the check and reason: `// NOLINT(bugprone-xxx): reason` or `// NOLINTNEXTLINE(...)`. `NOLINT` without check and blocks `NOLINTBEGIN` on file are not allowed.
- Comments in the code are in English; they explain “why” rather than “what”. Public headers provide a brief description of the object’s purpose.

## 12. Tests
- GoogleTest and GMock; the test name is `Suite.BehaviourUnderCondition`. It involves one test file for each module or class.
- Domain tests do not use real time or threads: they use deterministic port implementations - `libs/sim`.
- Coverage: 90% of lines and 80% of branches for the domain, and 75% of lines per module for the infrastructure.

## 13. Self-check checklist for review
1. The names reflect the domain language ([glossary](../domain/glossary.md)).
2. All errors are handled explicitly; none are lost. The invariants of the aggregate have been tested.
3. There are no unnecessary copies or allocations on the hot path; buffer sizes are limited.
4. External data is verified at the boundary; secrets do not get logged.
5. Any contract changes are accompanied by updates to `contracts/` and follow `task contracts:check`.
6. Significant decisions are documented using ADR.
