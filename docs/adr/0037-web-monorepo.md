# ADR-037. Web monorepo: tools, client generation and quality gates

<!-- language-links -->
[English](0037-web-monorepo.md) | [Русский](../ru/adr/0037-web-monorepo.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-001, ADR-019, ADR-026, ADR-030, ADR-031, ADR-034, [TypeScript standard](../engineering/typescript-coding-standard.md), requirement SR-23

## Context

The pnpm/Turborepo monorepo contains the application, design system, generated OpenAPI client, realtime client, linters, Vitest, Playwright and bundle-size budgets. The API client must regenerate automatically from contracts; budget violations block merging. The web stack uses React 19, TypeScript, Vite, TanStack Query and Router, Zustand, Tailwind CSS 4, shadcn/ui, Vitest and Playwright. The TypeScript standard requires strict compilation, unhandled-promise linting with zero warnings, formatting, at least 80% logic coverage and no manual generated-client edits. Task runs pinned tool containers (ADR-030, SR-23) in the local pipeline (ADR-034).

## Options considered

### Linter and formatter

| Option | Advantages | Disadvantages |
|---|---|---|
| Biome | One fast tool | Incomplete type-aware unhandled-promise and exhaustive-`switch` rules required by the standard |
| **ESLint with typescript-eslint `strictTypeChecked`, react-hooks and jsx-a11y, plus Prettier** | Type-aware rules; de facto standard | Two tools; slower |

### OpenAPI client

| Option | Advantages | Disadvantages |
|---|---|---|
| orval, openapi-generator | Generate hooks and classes | Large generated code and custom runtime; openapi-generator requires Java |
| **openapi-typescript types plus openapi-fetch, 6 KB** | Contract-derived path, parameter and response types; thin `fetch` wrapper | TanStack Query hooks are implemented above it |

Every build runs `turbo run gen` before linting, type checks, tests and builds. Output goes to untracked `src/gen`: clients cannot lag behind contracts, and incompatible contract changes fail console type checking.

### Realtime types

Proto defines the protocol (ADR-019). **protobuf-es**, through `protoc-gen-es` and `buf generate` from the contracts module with imports, generates frame types and canonical proto3 JSON from the same source as the server.

### TypeScript 7

Use **TypeScript 6.0** because the pinned typescript-eslint version supports TypeScript below 6.1. Adopting the native 7.x compiler requires typescript-eslint support and pipeline revalidation.

### Node and package manager

The container uses Node 24 LTS, digest-pinned `node:24.21.0-trixie`; the full image supplies root certificates required by buf dependencies. Corepack selects pnpm 12 from `packageManager`. pnpm installation scripts are prohibited except explicitly approved `@bufbuild/buf`, protecting the supply chain. E2E uses a Playwright image matching `@playwright/test`.

## Decision

1. **`web/` layout** (ADR-001): `apps/console` uses Vite, React 19, TanStack Router and Query, Zustand and Tailwind 4; `packages/design-system` provides Tailwind 4 tokens, dark theme by default, `cn` and shadcn/ui-style components; `packages/api-client` implements REST v1; `packages/realtime-client` implements realtime. Internal packages use TypeScript sources through `workspace:*`, without separate builds.
2. **Realtime client:** ADR-019 authentication, resumable subscriptions, token updates after every delta and `Subscribed`, exponential-backoff reconnection with jitter and the latest token, `ResyncRequired` -> a fresh snapshot, keepalive, reauthentication and `VersionGate` for newer-only deltas. Injectable sockets and timers allow network-free logic tests.
3. **Gates:** `task web:check` runs Prettier and Turborepo `turbo run gen lint typecheck test build size`; ESLint uses `--max-warnings 0`; `tsc` follows strict standard settings; Vitest enforces 80% lines, branches, functions and statements per package. size-limit budgets are ≤ 150 KB gzip for console JavaScript and ≤ 15 KB CSS. `task web:e2e` runs Playwright Chromium against the built console.
4. **Pipeline:** `web:check` is included in `task check`. The `web` stage of `task ci` runs `web:check` and `web:e2e` after changes to `web/`, OpenAPI or proto, and always in `ci:nightly`. Trivy scans `pnpm-lock.yaml` through `security:deps` (SEC-02).
5. **Commands:** `task web:check`, `web:e2e`, `web:test`, `web:gen`, `web:dev` at `127.0.0.1:5173`, `web:lock` and `web:format:fix`.

## Consequences

- Contract changes are immediately checked against the console; incompatible REST or realtime changes fail `typecheck`.
- The empty console shell is about 111 KB JS gzip for React, Router and Query. Further growth uses route-based code splitting and deliberate budget revisions.
- npm dependency licenses and web-artifact SBOMs accompany web delivery.
- E2E includes axe-core accessibility checks (ADR-031, ADR-076).

## Verification

Run `task web:check` and `task web:e2e`. Negative checks: a 50 KB JS budget fails size-limit; an 80% branch threshold rejected the realtime client's 77.96% before additional tests; ESLint rejects coding-standard violations.

## Design-system refinement

[ADR-062](0062-design-system-and-stable-updates.md) proposes a combined 180 KB JS gzip budget for the console and separate component showcase. The operational console's initial transitive Vite-manifest imports retain the 150 KB JS gzip limit; CSS remains within 15 KB gzip. Unused showcase and dialog code must not increase initial loading.

[ADR-065](0065-ui-loading-budgets.md) refines the expanded scope and measurements: 260 KB total JS gzip, with unchanged 150 KB initial JS and 15 KB CSS limits.
