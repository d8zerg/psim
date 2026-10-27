# Web Application

<!-- language-links -->
[English](README.md) | [Русский](../docs/ru/repository/web/README.md)
<!-- /language-links -->

The pnpm monorepo and Turborepo ([ADR-037](../docs/adr/0037-web-monorepo.md)); standard - [TypeScript](../docs/engineering/typescript-coding-standard.md).

| Package                    | Purpose                                                                                                                                                   |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `apps/console`           | Operator console and administration (Vite, React 19, TanStack Router and Query, Zustand, Tailwind CSS 4); shell, console, and administrative forms |
| `packages/design-system`          | Tailwind 4 tokens (dark theme by default), `cn`, shadcn/ui components                                                                     |
| `packages/api-client`           | REST v1: types from `contracts/openapi/psim-api-v1.yaml`, openapi-fetch, errors RFC 9457                                                                |
| `packages/realtime-client`            | Realtime channel (ADR-019): types from `contracts/proto/psim/realtime/v1`, rehydration, reconnection, `VersionGate`                                  |

Clients are generated from contracts during each build in `src/gen` and are not stored in git.

```
task web:check      # Formatting, generation, ESLint, types, Vitest coverage, build, budgets, visual regression and accessibility
task web:e2e        # Playwright against the built console
task web:dev        # Development server at http://localhost:5173
task web:catalogue  # Ladle component catalogue at http://localhost:61000
task web:benchmark  # Separate large-collection measurement in Chromium
task web:changeset:status # Version plan without changing version files or Git
task web:stability  # Preserve rows, focus and position during updates and connection loss
task web:lock       # Update pnpm-lock.yaml after changing package.json
```

Node and pnpm are not required on the host: commands are executed in the container `node:24` (pnpm 12 via corepack).

The console uses PKCE login and an incident feed with a snapshot and WebSocket deltas. The actual test environment and browser testing are [end-to-end environment](../docs/engineering/event-flow.md), `task flow:up`, and `task flow:test`.

The design system and mockups are available in `/design-system.html` after `task web:dev`. The themes, density, ru/en, acknowledgments, and updates without flickering are described in the [guide](../docs/engineering/design-system.md). Accessibility testing is `task web:a11y`.

Extended scope and excluded operator runs are described in the [acceptance record](../docs/engineering/design-system-acceptance.md). Visual baselines for the showcase and catalog are updated separately: `task web:visual:update` only runs tests `@visual`; the regular test compares baselines without automatic acceptance and runs the entire Axe matrix. Modes are tested with separate snapshots of the showcase, without multiplying component baselines. The catalog can be tested separately through `task web:catalogue:check`. Product and demo dictionaries are loaded separately; agreements are in the [ru/en glossary](../docs/engineering/ui-glossary.md).

The working shell, role-based routes, and session recovery are described in the [guide](../docs/engineering/application-shell.md). `task shell:test` checks for real five-minute connection breaks and updates expired tokens.

Administration, integration, and access boundaries are described in the [guide](../docs/engineering/administration.md). `task admin:test` performs settings through a browser with real services, PKCE with MFA, imports, certificates, rule checking, and scenario publishing.

The full [web client qualification](../docs/engineering/web-client-quality.md) is launched through `task web:qualify`. Individual tasks: `web:loading` - sizes, cold and warm caches, and chunk recovery; `web:perf` - feed performance; `web:native` - end-to-end scenarios with real services. [Comparison of options](../docs/engineering/web-client-loading.md) preserves current budgets.
