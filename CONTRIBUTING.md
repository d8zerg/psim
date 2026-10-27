# Contributing to Open-Source PSIM Platform

<!-- language-links -->
[English](CONTRIBUTING.md) | [Русский](docs/ru/repository/CONTRIBUTING.md)
<!-- /language-links -->

The MVP is under development. Before contributing, read the [README](README.md), [product scope](docs/product/mvp-scope.md), [release conditions](docs/product/release-readiness.md) and [architecture decisions](docs/adr/README.md).

## Proposals and bugs

Open an [issue](https://github.com/d8zerg/psim/issues) describing the task, expected behavior and a usage example. For a bug, include the version or commit, OS, installation method, reproduction steps and logs with secrets removed.

Report security issues according to [SECURITY.md](SECURITY.md).

Discuss changes to MVP scope, public contracts or architecture decisions in an issue first, then record them in an ADR. Small fixes can be submitted directly as pull requests.

## Development and verification

C++ is the project's primary language. Services, connectors, `psimctl`, business logic and administrative-operation executors are implemented in C++. Python is appropriate for supporting build, generation, deployment and test tasks. Boundaries and verification results are described in the [runtime language rules](docs/engineering/product-runtime-languages.md).

The officially supported OS is Debian 13 x86_64. Development, builds, tests and local pipelines use native Docker Engine on Linux with the Docker Compose plugin. Builds use pinned container tools; setup is described in the [README](README.md#development). A [dev container](.devcontainer/devcontainer.json) uses the same host Docker Engine.

1. Create a branch from the current main branch and implement the change.
2. Follow the [C++ standard](docs/engineering/cpp-coding-standard.md) and [TypeScript standard](docs/engineering/typescript-coding-standard.md).
3. Update documentation and contracts when behavior changes. Follow the [compatibility policy](docs/engineering/compatibility-and-support.md).
4. Run `task ci` before every commit. For a significant behavior change, add a check reproducing the problem or establishing the new invariant.
5. Open a pull request describing the problem, resulting behavior, checks performed and limitations. Reference an issue when available.

For the rule parser and validator, `task fuzz:rules` runs ten minutes of fuzzing with ASan/UBSan in the toolchain container. The corpus, log and report are stored in `build/rules/fuzz/`. This run is part of `task ci` when relevant components change. Release qualification requires a 24-hour run of every fuzz target without crashes or sanitizer errors.

`task rules:test`, `task rules:sanitizers` and `task rules:coverage` check rule management and isolated historical runs with real PostgreSQL, Kafka and ClickHouse. Semantics and limitations are described in the [rules guide](docs/engineering/correlation-rules.md).


**Documentation languages.** English is the default in the repository, portal and release archive. The [shared glossary](docs/domain/glossary.md) maps EN/RU terminology; the [UI glossary](docs/engineering/ui-glossary.md) fixes interface wording. Update both language versions in the same change, preserving API identifiers, paths, commands, requirement identifiers and ADR numbers.

The page registry is `docs/localization.yaml`. Keep page identifiers stable when moving files, update both paths and relative links, and register both counterparts for a new page. Matching heading levels define section correspondence. A missing translation never falls back to another language.

Run maintenance commands inside the dev container:

1. Edit the English source and its Russian counterpart, then review their meaning and executable examples together.
2. For contract changes, run `task contracts:gen`. Update reviewed descriptions in `tools/docs/locales/artifacts.yaml` and `tools/docs/locales/proto.yaml`; run `python3 -I tools/docs/proto_reference.py . --update`. For dashboards, run `python3 -I tools/docs/localized_assets.py . --update` after updating the shared description catalog.
3. Record the review of explicitly selected pairs with `task docs:review -- CONTRIBUTING.md` (replace the example path with the changed pages). This validates structure and identifiers before updating hashes; it does not establish translation accuracy by itself. HTML runbook pairs use the same registry and review command.
4. Run `task docs:check`. It rejects missing or stale counterparts, inconsistent identifiers and examples, broken links and Russian prose left in English pages, then checks the portal and standalone archive in both languages.

## Data and license

The project is distributed under the [MIT license](LICENSE). Submit only code and materials you are entitled to distribute under those terms. New dependencies must pass the existing CI license and vulnerability checks.

Use synthetic data in examples and tests. Do not include active keys, passwords, certificates containing private keys, real personal data or customer floor plans. Demonstration connectors and scenarios must be reproducible without access to someone else's equipment.
