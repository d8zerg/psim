# ADR-034. Local CI/CD and software supply chain

<!-- language-links -->
[English](0034-local-ci-and-supply-chain.md) | [Русский](../ru/adr/0034-local-ci-and-supply-chain.md)
<!-- /language-links -->

- **Status:** Accepted
- **Refinement:** [ADR-036](0036-no-git-hooks.md) replaces background Git-hook execution and the `pre-push` gate. Run `task ci` before committing, with optional committed-state verification through `task ci:run`.
- **Related:** ADR-030, ADR-031, ADR-032, ADR-033, requirements SR-23 and SR-24 ([threat-register.md](../security/threat-register.md)), [strategy.md](../quality/strategy.md#4-gates)

## Context

Delivery requires a pipeline from commit to signed artifact: build, static analysis, tests, sanitizers, contracts, integration tests, benchmarks, packages, images, CycloneDX SBOM, license and vulnerability checks, and signing. Every change must pass the full pipeline; release artifacts require SBOMs and signatures.

Development and pipelines run locally on the developer's Linux environment using native Docker Engine and the Docker Compose plugin. Git hosting stores code only and runs no pipelines. One engineer and an AI agent develop the project; the engineer commits and pushes.

SR-23 requires dependency lockfiles, digest-pinned base and tool images, SBOMs, blocking license and vulnerability checks, and reproducible builds. SR-24 requires artifact signing, installer signature verification and non-root containers with read-only filesystems and no unnecessary capabilities.

## Options considered

### Pipeline execution

| Option | Advantages | Disadvantages |
|---|---|---|
| Hosted CI, such as GitHub Actions | Server-side gate | Conflicts with local execution; remote runners and hosted signing secrets |
| Local Jenkins or Woodpecker server | Server-side gate | Another system for one engineer; duplicates Task |
| **Local Task jobs and a `pre-push` hook** | Same tasks as manual development (ADR-030); same qualification environment; no external system | Client-side gate can be bypassed with `--no-verify`; per-commit pipeline records expose bypasses |

### Artifact signing

| Option | Advantages | Disadvantages |
|---|---|---|
| cosign (Sigstore) | OCI standard | Default internet transparency log and OIDC; images need a registry; target systems lack cosign |
| GPG-signed APT repository | Native apt mechanism | Packages only; GPG keyring management; excludes images |
| **Ed25519 through OpenSSL 3 over SHA256SUMS** | Offline; one mechanism for packages, OCI archives and SBOMs; Debian 13 x86_64 provides `openssl` and `sha256sum` | Custom release-directory format; registry images and out-of-scope Kubernetes would need a separate cosign ADR |

### Container base

| Option | Advantages | Disadvantages |
|---|---|---|
| `debian:trixie-slim` | Native Debian 13 packages and debugging shell | More packages; root by default |
| `distroless/base-nossl-debian13:nonroot` | No shell; UID 65532 | No system C++ runtime |
| **`distroless/cc-debian13:nonroot`** | No shell; native libstdc++ and libgcc; UID 65532 | Debug through a separate image or `docker cp`; Trivy scans base packages |

### Push-gate duration as code grows

| Option | Advantages | Disadvantages |
|---|---|---|
| Synchronous full run on every push | Simple | Growing clang-tidy, four build/test variants, coverage and benchmarks block work |
| Heavy checks only nightly | Fast push | Sanitizer and coverage failures reach `master` |
| **Background post-commit run, affected-scope checks, full nightly and release runs** | Results usually precede push; duration follows change size; failures persist no longer than a day | Scope calculation from target graph; separate clone and build directories |

### Benchmark gate

The regression threshold is 5%. Local noise can falsely fail mean-based comparisons. Use minimum CPU time over 10 repetitions; suspected regressions receive up to two reruns, merged by minimum. Reruns can only reduce the current minimum, filtering random outliers without masking a real regression. Baselines are measured on Linux with native Docker Engine and repeated after environment changes.

## Decision

1. **Pipelines** are root Taskfile jobs following [quality gates](../quality/strategy.md#4-gates):
   - `task ci` checks each push's change scope: all-preset `-Werror` builds, fitness functions, formatting, clang-tidy, unit tests, ASan+UBSan, TSan, coverage, Debian 13 x86_64 execution, contracts, documentation, `security:deps`, `bench:compare`, package installation and image checks. `FULL=true` selects the full scope. Integration tests use local Docker Compose.
   - `task ci:nightly` runs full-scope `ci`, reproducibility and image scans.
   - `task release` runs `ci:nightly`, creates a signed release directory and verifies it.
2. **Push scope**, in `tools/ci/scope.py` and `scope.yaml`, compares the working tree with the merge base against `origin/master`. Affected targets include changed modules and all transitive dependants in the CMake File API graph. Builds remain complete and incremental. Expensive stages select affected translation units, target-labelled ctest tests and sanitizers, coverage modules and benchmarks. Changed paths select contracts, full documentation checks, dependency scans, packages and images. Build-infrastructure changes to CMake, Conan, profiles, lint/format rules, Taskfiles or toolchain, or a missing `origin/master`, select the full scope.
3. **Original background workflow:** `post-commit` ran `tools/ci/runner.sh` asynchronously against exactly committed content in persistent clone `.cache/ci/repo`, with separate incremental builds and shared Conan, ccache and Trivy caches. Eight parallel jobs kept the environment responsive. Only one run executed at a time; requests for commits no longer at HEAD were skipped. Results were `.cache/ci/<commit>.ok` or `.failed`, with logs and desktop notifications exposed by `task ci:status`, `task ci:log` and `task ci:run`.
4. **Original push gate:** `pre-push` accepted `.ok` commits, waited for active runs or started missing runs in the clone, unaffected by uncommitted changes. `--no-verify` was an emergency bypass visible through `task ci:status SHA=<commit>`. The refinement above replaces these hook rules.
5. **Packages:** CPack DEB through `cmake/PsimPackaging.cmake`; `psim-<component>` for Debian 13 amd64 with fixed file timestamps. `dpkg-shlibdeps` derives dependencies from actual symbols; libstdc++ and libgcc are dynamic Debian 13 dependencies (ADR-032). `task release:package:test` installs, starts and removes packages in a clean target-OS image.
6. **Images:** `deploy/images/<name>/Dockerfile` uses digest-pinned `distroless/cc-debian13:nonroot`, `linux/amd64`, UID 65532 and OCI version/commit labels. `task release:images:test` checks non-root startup with `--read-only`, `--cap-drop ALL` and `no-new-privileges`.
7. **SBOM:** CycloneDX 1.6 from `tools/release/sbom.py`, containing host-context Conan application dependencies without test dependencies, since gtest and benchmark use `test_requires`, and dynamic libstdc++/libgcc with GCC Runtime Library Exception. Conan supplies the graph. Every release artifact has its own SBOM and SHA-256; image SBOMs add Trivy OS packages. SBOM time is the commit time; serial numbers derive from contents.
8. **Vulnerabilities and licenses:** Trivy 0.75 (ADR-031). `security:deps` checks `conan.lock` and the application SBOM, blocking HIGH and CRITICAL vulnerabilities. `tools/release/licenses.yaml` allowlists application licenses; missing or unlisted licenses block delivery, and statically linked copyleft code is prohibited. `security:images` blocks fixable HIGH/CRITICAL image findings; unfixed findings remain visible in nightly reports.
9. **Signing:** `tools/release/sign.sh` and `verify.sh` manage `dist/psim-platform-<version>-<commit>/`, containing packages, image archives, SBOMs, `SHA256SUMS` and Ed25519 `SHA256SUMS.sig`. Verification trusts only a separately supplied key through `PSIM_TRUSTED_KEY` or `deploy/keys/psim-release.pub.pem`, never a key supplied by the release directory. It requires matching hashes, no unlisted files and an artifact-hash SBOM for every artifact. The installer uses the same `verify.sh`.
10. **Keys:** the owner creates the release key through `task release:keygen DIR=<protected-directory>`. Keep the private key outside the repository and supply it through `PSIM_SIGNING_KEY`; publish the public key as `deploy/keys/psim-release.pub.pem` for the first release. Before that, use development key `.cache/keys/psim-dev.pem`, explicitly identified during verification.
11. **Pinning (SR-23):** tool and base images, including toolchain, buf, golang, distroless, Trivy and target platforms, use tags and digests; C++ dependencies use `conan.lock`. LLVM apt.llvm.org packages inside the toolchain are not version-pinned because old builds are not retained. FF-13 compares builds byte for byte within one image, identified by its Dockerfile hash.
12. **Benchmarks:** `tools/bench/bench.py` and `psim_add_benchmark` use Google Benchmark in `release`. Baseline results come from the merge-base commit with `origin/master`, cached in `.cache/bench/<platform>/<commit>.json`; if absent, build that commit from `git archive` with current compiler settings and Conan profile. Baseline and current versions use the same environment, with a 5% minimum-CPU-time threshold and reruns.

## Consequences

- Pipeline and acceptance checks use the same environment. Push usually does not wait; execution time follows change size. Failures outside the selected scope, such as an untracked shared-header dependency, are found by the next nightly run.
- The original client-side gate combined hooks with visible result records. A team or shared environment requires reconsideration through a server-CI ADR; the refinement above defines the current hook-free workflow.
- Quality environment `E-CI` is the developer environment running `task ci`.
- New installable components add `install(... COMPONENT)`, `psim_package()`, an optional `deploy/images/<name>/Dockerfile` and an `IMAGES` entry.
- Registry image distribution and cosign signing are outside MVP, as is Kubernetes; adopting them requires another ADR.

## Verification

Run `task ci`, `task ci:nightly` and `task release`; scope tests are `tools/ci/test_scope.py`, included in `task arch:selftest`. Negative checks must reject a 20% benchmark regression, vulnerable SBOM dependencies, prohibited or missing licenses, altered artifacts, unlisted files, missing SBOMs, an untrusted key and an altered manifest.
