# ADR-032. C++ toolchain: Debian 13 x86_64, Clang 21, libstdc++, Conan 2 and CMake presets

<!-- language-links -->
[English](0032-cpp-toolchain.md) | [Русский](../ru/adr/0032-cpp-toolchain.md)
<!-- /language-links -->

- **Status:** Accepted
- **Updated:** Debian 13 x86_64 is the only officially supported MVP operating system
- **Related:** ADR-001, ADR-030, ADR-031, ADR-034, requirements SR-23 and SR-24 ([threat-register.md](../security/threat-register.md))

## Context

The server uses C++23 and targets Debian 13 x86_64. Builds, dev-container development and execution validation use Debian 13. Developers may use any Linux host with native Docker Engine and the Docker Compose plugin; the Docker daemon runs directly on the host. Official support applies to the target server OS.

The project needs reproducible tools, one standard-library ABI for applications and dependencies, clang-tidy, sanitizers and coverage. Unsupported OS versions do not determine compiler or library selection. Versions are selected by C++23 capabilities, quality and verification results.

## Options considered

### Compiler and standard library

| Option | Advantages | Disadvantages |
|---|---|---|
| Debian 13 GCC and libstdc++ | Standard distribution tools and simple runtime | clang-tidy and llvm-cov require a separate LLVM toolset |
| Clang and libc++ | One LLVM toolset | Another standard library in the distribution |
| **Clang 21 and Debian 13 system libstdc++** | Compiler, analyzers and sanitizers from one release; native target-OS C++ runtime | Dependencies must use the same profile for one ABI |

### Build environment

| Option | Advantages | Disadvantages |
|---|---|---|
| Host tools | Convenient locally | Versions and packages depend on the developer's environment |
| **Debian 13 x86_64 container, also used as the dev container** | One reproducible environment; artifacts are checked on the target OS | First use requires image download and build |

### Dependencies

| Option | Advantages | Disadvantages |
|---|---|---|
| **Conan 2 with project profile and lockfile** | Pinned versions and recipe revisions; shared ABI; configurable library options | Initial dependency build takes tens of minutes |
| Distribution packages | Fast installation | Distribution determines versions and build options |
| vcpkg | C++ dependency management | A second manager is unnecessary |

## Decision

1. **Target OS:** Debian 13 x86_64 only. Additional distributions and architectures require a post-MVP decision and verification.
2. **Compiler:** Clang 21 and lld 21 from [apt.llvm.org](https://apt.llvm.org/); C++23 without extensions; Debian 13 system libstdc++. CMake accepts Clang and GCC; GCC uses the distribution linker. The official pipeline uses Clang 21.
3. **Linking:** use target-OS libstdc++ and libgcc dynamically. DEB dependencies are derived from actual symbols by `dpkg-shlibdeps` (ADR-034). Static C++ runtime linking or glibc-only dependencies are not required.
4. **Build container:** `tools/toolchain/Dockerfile` uses `debian:trixie`, pinned by digest in the root Taskfile. It includes Clang, clang-tidy, clang-format, llvm-cov, GCC with libstdc++ headers, CMake, Ninja, Conan, ccache, buf, Task and DEB tools. `.devcontainer/` uses the same image, identified by its Dockerfile hash.
5. **Dependencies:** Conan 2 uses `tools/conan/profiles/clang21-libstdcxx` for host and build contexts; versions are in `conanfile.py`, revisions in `conan.lock`. Application libraries default to static linking to avoid separate Conan packages at installation; this is a distribution choice, not an OS restriction. `conanfile.py` defines per-library options. Dependencies build in Release, with project configurations mapped through `CMAKE_MAP_IMPORTED_CONFIG_*`.
6. **CMake:** presets `debug`, `release`, `relwithdebinfo`, `asan` (ASan + UBSan), `tsan` and `coverage`; Ninja and ccache; builds in `build/debian13/<preset>`, caches in `.cache/debian13/`.
7. **Security and quality:** strict warnings and `-Werror` for project code; stack hardening, PIE, RELRO, nonexecutable stack, `_FORTIFY_SOURCE=3` outside Debug and `_GLIBCXX_ASSERTIONS` without ABI changes. Generated and third-party code do not receive project warnings.
8. **Reproducibility:** `-ffile-prefix-map`, `--build-id=sha1`, prohibited `__DATE__` and `__TIME__`, and `SOURCE_DATE_EPOCH=0`; artifacts embed version and commit. `task cpp:reproducible` compares two release builds byte for byte without ccache.
9. **Contracts:** protoc and `grpc_cpp_plugin` come from the Conan build context; generator and runtime versions match. `contracts:vendor` vendors external proto imports.
10. **Sanitizers:** ASan + UBSan check the application. TSan instruments host-context dependencies through `clang21-libstdcxx-tsan`; build-context tools use the ordinary profile. Instrumentation options contribute to package IDs, keeping variant caches separate.

## Consequences

- `task cpp:build` or `task cpp:test` runs the build in the target environment.
- Native installation uses Debian 13's standard C++ runtime; OCI images use `distroless/cc-debian13:nonroot` (ADR-034).
- Compiler and standard library agree throughout the dependency graph. Add libraries through Conan and update the lockfile.
- Language-standard and toolchain changes are justified by capabilities and checks, not unsupported-OS compatibility.

## Verification

`task cpp:test` builds and runs unit tests; `task cpp:runtime-test` runs tests and `psimctl` in clean Debian 13 x86_64 without a toolchain; `task release:package:test` installs, starts and removes DEBs with computed dependencies; `task cpp:reproducible` compares builds byte for byte; `task cpp:test:sanitizers` runs sanitizers.
