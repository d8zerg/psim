# TypeScript Coding Standard

<!-- language-links -->
[English](typescript-coding-standard.md) | [Русский](../ru/engineering/typescript-coding-standard.md)
<!-- /language-links -->

[ADR-033](../adr/0033-code-quality-gates.md) · Tools - [ADR-037](../adr/0037-web-monorepo.md)

This standard applies to `web/` (pnpm + Turborepo, React 19, Vite, TanStack Query and Router, Zustand, Tailwind CSS 4, shadcn/ui). The rules checked by the tool are specified in its configuration: `web/tsconfig.base.json`, `web/eslint.config.js`, `web/.prettierrc.json`, `vitest.config.ts`, and `.size-limit.json` packages. Below are their meanings and rules for review.

## 1. Quality Gates (`task web:check`, stage `web` of the pipeline)

| What | Requirement |
|---|---|
| Type checking | `tsc` with no errors in all packages |
| Linter | ESLint (typescript-eslint `strictTypeChecked`, react-hooks, jsx-a11y), `--max-warnings 0` |
| Formatting | Prettier without changes |
| Tests and coverage | Vitest; ≥ 80% of lines, branches, functions, and statements per package ([strategy.md](../quality/strategy.md#6-coverage)) |
| Delivery budgets | Initial load ≤ 150 KB gzip; full application graph ≤ 250 KB; showcase graph ≤ 260 KB; unique JS distribution ≤ 300 KB; CSS ≤ 15 KB ([ADR-065](../adr/0065-ui-loading-budgets.md), ADR-070, ADR-075) |
| Generated clients | REST - from OpenAPI, realtime - from proto; generated at each build in `src/gen`, not stored in git |
| E2E | Playwright for the built console (`task web:e2e`) |

## 2. Compiler

- General `tsconfig` with `strict: true`, `noUncheckedIndexedAccess`, `exactOptionalPropertyTypes`, `noImplicitOverride`, `noFallthroughCasesInSwitch`, `verbatimModuleSyntax`, `isolatedModules`.
- Only ES modules; goal is to meet modern browsers' requirements for web clients.

## 3. Types

- `any` is prohibited; for unknown data - `unknown` and narrowing. Network data is checked at the boundary (schemas generated from OpenAPI), beyond which the code works with verified types.
- Type assertions (`as`) and non-null `!` - only with a comment explaining why it’s safe.
- State options are exhaustive unions with an exhaustive `switch` (default in branches for `never`), not sets of flags.
- Object identifiers are branded types (`IncidentId`), not bare `string`.
- `enum` is not used; instead, it uses unions of string literals or objects `as const`.

## 4. Naming and files

| Entity | Style | Example |
|---|---|---|
| Variables, functions, hooks | `camelCase`, hooks with `use` | `incidentFeed`, `useIncident` |
| Types, interfaces, React components | `PascalCase` | `IncidentCard` |
| Module constants | `UPPER_SNAKE_CASE` | `MAX_FEED_SIZE` |
| Component files | `PascalCase.tsx` | `IncidentCard.tsx` |
| Other files | `kebab-case.ts` | `realtime-client.ts` |

## 5. React

- Only functional components and hooks; hook rules are checked by the linter.
- Server state - TanStack Query; client state - Zustand; local state - `useState`. Server data is not copied to Zustand.
- Side effects - in event handlers and requests, `useEffect` - only for synchronization with external systems.
- Accessibility: semantic elements, labels on control elements; axe-core check in E2E.
- Interface texts - through localization (ru, en); the server returns codes, not texts ([crosscutting.md](../architecture/crosscutting.md#15-internationalization)).

## 6. Errors and Asynchrony

- Each promise is handled: awaited, returned, or explicitly marked as background; “floating” promises are prohibited by the linter.
- API errors are resolved based on code from the error catalog (RFC 9457), not text.

## 7. Suppression

- Disabling linter rules - only for lines with a rule name and reason; disabling on files is not allowed.
