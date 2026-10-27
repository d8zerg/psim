# ADR-036. Remove Git hooks and run `task ci` before committing

<!-- language-links -->
[English](0036-no-git-hooks.md) | [Русский](../ru/adr/0036-no-git-hooks.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** refines [ADR-033](0033-code-quality-gates.md), decision 8 on `pre-commit`, and [ADR-034](0034-local-ci-and-supply-chain.md), decisions 3-4 on post-commit background runs and `pre-push`

## Context

ADR-033 introduced fast `pre-commit` checks. ADR-034 added background `task ci` through `post-commit` and a `pre-push` hook requiring a successful pipeline record. The working procedure already runs `task ci` before committing; hooks duplicate checks and add waiting and maintenance.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Keep hooks | Automatic gate | Duplicates the established `task ci` -> commit workflow; delays commits and pushes; another maintained mechanism |
| **Remove hooks; require `task ci` before every commit** | One mechanism; immediate commits and pushes | Relies on discipline; missed runs are caught by nightly checks |

## Decision

1. Remove `pre-commit`, `post-commit`, `pre-push` and `hooks:*` tasks; do not use `core.hooksPath`.
2. Run `task ci` before every commit, using ADR-034 decision 2 for scope. Commit only after a passing run.
3. Retain optional `task ci:run [SHA=<commit>]`, which tests committed content in clean clone `.cache/ci/repo` with shared caches and separate build directories. It catches problems masked by uncommitted or untracked working-tree files. `task ci:status` and `task ci:log` expose results.
4. `task ci:nightly` and `task release` continue to check the full scope.

## Consequences

- Commits and pushes do not wait for a pipeline.
- The merge gate is a working rule rather than a Git mechanism. Additional contributors require reconsidering hooks or server-side CI.

## Verification

`task ci:nightly` checks the full scope; `task ci:run` verifies committed state before release.
