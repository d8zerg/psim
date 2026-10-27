# ADR-001. Monorepo structure and module boundaries

<!-- language-links -->
[English](0001-monorepo-structure.md) | [Русский](../ru/adr/0001-monorepo-structure.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** [context-map.md](../domain/context-map.md), [crosscutting.md](../architecture/crosscutting.md#12-testability-and-module-structure)

## Context

The platform contains approximately 12 C++ services, platform libraries, contracts, a web application, connectors, deployment artifacts and documentation. One engineer and an AI agent develop it. Atomic contract-and-code changes, a shared build and automatic module-boundary checks are important.

## Considered options

### Option A. Multiple repositories, one per service

- Independent versions and releases.
- Contract changes require coordinated changes across repositories and duplicated build infrastructure: pure overhead for one engineer.

### Option B. Modular monolith, one process

- Simpler deployment and debugging.
- Conflicts with horizontal scaling by pipeline stage and zero-downtime upgrades by service.

### Option C. Monorepo with independent services, organized by layers with top-level `domain/` and `adapters/`

- Shared build.
- A context's code is scattered through the tree; context boundaries are not visible.

### Option D. Monorepo with independent services, organized by context

- Shared build and atomic changes; context boundaries align with directories and CMake targets, enabling automatic dependency checks.
- Requires discipline in dependency direction, enforced by a fitness function.

## Decision

Option D, with this structure:

```text
contracts/                  public contracts, the single source of truth
  proto/psim/...            Protobuf: envelope, topic events, connector protocol, REST DTOs
  asyncapi/                 topic descriptions
  taxonomy/                 machine-readable event taxonomy
  errors/                   error-code catalog
libs/
  platform/                 shared libraries: runtime, async, kafka, pg, redis, security,
                            observability, config, connector, timers, identity, files
  sim/                      deterministic simulation environment
  domain/<context>/         domain cores: catalog, ingest, processing, incident, response,
                            access, audit; pure libraries
services/<service>/         application + adapters + main; one executable per service
connectors/
  common/                   shared reference-connector functions
  simulator/                simulator and load generator
  webhook/                  reference Webhook/HTTP connector
web/                        pnpm + Turborepo: apps/console, packages/*
tools/psimctl/              administration CLI
tools/connectors/           protocol conformance, load and integration checks
deploy/                     compose, systemd, deb, installer, observability
tests/                      e2e, load, chaos, contract
docs/                       documentation, ADRs, architecture
```

Dependency rules:

1. `libs/domain/*` depends only on the standard library and `libs/domain/common`, containing shared value types. No Boost.Asio, librdkafka, libpq, networking or system clocks.
2. `services/*` depends on its own domain core, `libs/platform` and generated contract code. A service does not depend on another service.
3. Domain cores of different contexts do not depend on one another; they communicate through contracts.
4. Correlation-rule management is a module of Correlation Engine, not a separate service.
5. Timers are provided by the `libs/platform/timers` library, not a separate service.

## Consequences

- Contract, code and documentation changes belong to one commit; CI checks them together.
- The MVP services are Connector Gateway, Resource Catalog, Normalizer, Event History, Correlation Engine, Incident Service, Response Engine, Command Service, Notification Service, Projection Service, Audit Service and API Gateway.
- Full-tree build time grows; compilation caching and building only affected CI targets mitigate this.

## Verification

Architecture checks analyze the CMake target and include graphs. Violations of rules 1-3 block merging.
