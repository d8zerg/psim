# ADR-030. Task as the development task entry point

<!-- language-links -->
[English](0030-task-runner.md) | [Русский](../ru/adr/0030-task-runner.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-001

## Context

Builds, generation and checks use Task rather than unrelated scripts. The repository already includes contract generation and validation, documentation checks for links, typography, PlantUML and C4, C++ builds with CMake presets and Conan, web builds with pnpm and Turborepo, Docker Compose environments, tests and packages. Developers, AI agents and CI need the same entry point, task dependencies and incremental skipping when inputs have not changed.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Shell scripts | No dependencies | No shared task list, dependency model or incremental execution; scripts proliferate |
| GNU Make | Widely available | Tabs and nonobvious syntax; awkward variables and multiline commands; file-target-only incrementality |
| just | Simple syntax | No input/output incrementality or namespaced module task files |
| CMake custom targets | Already part of C++ builds | Do not cover web, documentation or environments; every task requires CMake configuration |
| **Task (taskfile.dev)** | YAML, one static binary, dependencies, `sources`/`generates` incrementality, namespaced module Taskfiles, `task --list` help, MIT license | Another development tool |

## Decision

1. **Task 3.x** is the single entry point: `task <namespace>:<task>`. Its version is pinned in the development container; host installation uses a release binary with checksum verification.
2. **Structure:** root `Taskfile.yml` defines common variables for pinned images and caches, plus aggregate tasks `check`, `gen`, `clean` and `tools:pull`. Modules include their `Taskfile.yml` as namespaces: `contracts`, `docs`, then `cpp`, `web`, `env` and `deploy`.
3. **Complex logic** belongs in `tools/<area>/*.py`, invoked by short, readable tasks. Do not add separate shell-script entry points.
4. **Tools** run in containers whose image versions are pinned in the root Taskfile. Local orchestration requires Docker, Python 3 with PyYAML and Task.
5. **CI** calls the same `task check` as developers: a passing local check should pass CI.
6. CMake and pnpm remain their components' build systems; Task invokes them.

## Consequences

- Running `task` without arguments lists all tasks and descriptions as development help.
- `tools/contracts/check.sh` and `contracts/buf.sh` were removed; their operations became `contracts:*` tasks.
- New components add tasks to module Taskfiles rather than separate scripts.

## Verification

Review new entry points for Task-only execution; CI runs `task check`.
