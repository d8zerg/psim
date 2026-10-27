# Languages for Component Implementation

<!-- language-links -->
[English](product-runtime-languages.md) | [Русский](../ru/engineering/product-runtime-languages.md)
<!-- /language-links -->

Status: Accepted.

The primary language of the project is C++23. Own server components, connectors, libraries, administrative CLI, and recovery logic are implemented in C++. The web client uses TypeScript. C++ components must not delegate product operations to a Python dispatcher.

## Verified Boundaries

| Domain | Implementation and Solution |
|---|---|
| `services/`, `libs/`, `connectors/` | No product Python code detected; primary code is C++ |
| `tools/psimctl/` | Administrative operations, backup, recovery, and diagnostics are in C++; Python is used only in the test environment |
| Monitoring Agent | A separate C++ component `psim-monitoring-agent`, transferred from Python; TLS, protocol checks, metric collection, and HTTP handlers are in C++ |
| Release Migration Executor | Transferred from `release_migrations.py` to `psim-pg-migrate release`; locks, checksums, resumption, and contract gates are implemented in C++ |
| `tools/deploy/`, `tools/vm/` | Python is allowed for generating configuration, installation, delivery, managing the test environment, and qualification scenarios |
| `tools/contracts/`, `tools/docs/`, `tools/arch/`, `tools/quality/`, `tools/ci/` | Python is allowed for generation and validation; these programs do not handle product requests |
| Testing HTTP receivers and providers in `tools/` | External system replacements in tests; not part of the product runtime |
| Patroni | External infrastructure dependency; the language of the external product does not determine the language of the components |
| Web client and static resource servers | Existing infrastructure for the web client; the C++ API Gateway implements the business API |

## Automatic Verification

`task arch:language` is included in `task arch:check`. The check rejects Python from product directories outside of tests and external code, Python-entrypoints from deployment images, and known forms of transferring operations from C++ to Python. Module-level verifications confirm the detection of such violations. This is a check for specific architectural boundaries; a thorough review of new auxiliary scripts is still required.

References: [architecture](../architecture/README.md), [C++ toolchain](../adr/0032-cpp-toolchain.md), [auxiliary tasks](../adr/0030-task-runner.md).
