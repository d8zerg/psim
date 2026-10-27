# ADR-031. Verification tools

<!-- language-links -->
[English](0031-verification-tooling.md) | [Русский](../ru/adr/0031-verification-tooling.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-030, [strategy.md](../quality/strategy.md), [verification.yaml](../quality/verification.yaml)

## Context

The baseline suite uses GoogleTest, GMock, Google Benchmark, libFuzzer, Vitest and Playwright. Quality strategy additionally requires invariant property tests, coverage, load generation without distorted latency tails, network-fault injection, REST contract tests, dependency and image scans, DAST and TLS checks. All tools run through Task (ADR-030), with pinned containers on native Docker Engine. Licenses must permit CI use for a commercial product.

## Options and decision

| Workload | Selected | Alternatives | Rationale |
|---|---|---|---|
| C++ property tests | **RapidCheck** (BSD-2) | Custom GoogleTest generators; fuzzing as properties | Generators, counterexample shrinking and GoogleTest integration |
| C++ coverage | **llvm-cov**, Clang source-based coverage | gcov/lcov | Same Clang toolchain; accurate branch coverage |
| Latency histograms | **HdrHistogram_c** (CC0/BSD) plus Prometheus native histograms | Prometheus buckets only | Accurate report tails without bucket error |
| Load generation | **Custom simulator**, open workload model | k6, Gatling, ghz | Connector credits, bidirectional streaming, reconciliation sequences and oracle-backed scenario injections need custom code in external generators |
| Console and REST load | **k6**, AGPL, used externally and not distributed, with WebSocket | Gatling, Locust | JS scenarios, WebSocket and percentile metrics; excluded from the distribution |
| Network faults | **Toxiproxy** (MIT) and `tc netem` on test nodes | Pumba, Chaos Mesh, Litmus | Kubernetes is outside MVP; Toxiproxy is test-API controlled and runs in Compose and on VMs |
| Process and node failure | **Task chaos scenarios**: `docker kill`, `systemctl kill`, VM shutdown through the test hypervisor API | Chaos Mesh, Litmus | Same topology constraints; experiments are versioned with code and invoke reconciliation |
| REST contract testing | **Schemathesis** (MIT) using `psim-api-v1.yaml` | Dredd, custom tests | OpenAPI request generation, response-schema checks and stable OpenAPI 3 support |
| UI accessibility | **axe-core** (MPL-2.0) in Playwright | Lighthouse | E2E integration and deterministic rules |
| Dependencies, licenses and images | **Trivy** (Apache 2.0) | Grype + Syft, OWASP Dependency-Check | One tool for SBOM, vulnerabilities, licenses and images |
| DAST | **OWASP ZAP** (Apache 2.0) | Burp Suite | Open license, OpenAPI import and automation |
| TLS checks | **testssl.sh** (GPL-2.0), an external tool | sslyze | Comprehensive checks and container execution |
| Static security analysis | **clang-tidy** with bugprone, cert, performance and project rules | CodeQL, Coverity | Already in the toolchain; CodeQL and Coverity licensing restricts closed-source use |

Pin tool versions in the root Taskfile for images or lockfiles for libraries when adding each tool. Methods and commands are in the [verification matrix](../quality/verification-matrix.md#5-method-catalog).

## Consequences

- The load generator is the reference simulator connector, part of the product, and evolves with the protocol and reconciliation oracle.
- AGPL/GPL tools k6 and testssl.sh run only as external CI and test-environment programs and are excluded from the distribution.
- Chaos experiments work in Compose and on VMs. Kubernetes adoption requires a separate ADR.

## Verification

`task docs:verification` requires every automated registry method to define a Task command, pass criterion and gate; manual methods require a verification procedure.
