# Прогресс разработки MVP

Статусы: не начат · в работе · готов (ждёт утверждения) · утверждён

План: [psim-mvp-development-plan.md](psim-mvp-development-plan.md)

## Ф0. Архитектура и контракты -> M0

| Шаг | Название | Статус | Артефакты |
|---|---|---|---|
| 0.1 | Видение продукта и границы MVP | утверждён | [product-brief](product/product-brief.md), [roles-and-journeys](product/roles-and-journeys.md), [mvp-scope](product/mvp-scope.md), [acceptance-spec](product/acceptance-spec.md) |
| 0.2 | Доменная модель и контексты | утверждён | [glossary](domain/glossary.md), [context-map](domain/context-map.md), [aggregates](domain/aggregates.md), [state-models](domain/state-models.md), [event-taxonomy](domain/event-taxonomy.md) |
| 0.3 | Архитектурный документ | утверждён | [arc42](architecture/README.md), [runtime](architecture/runtime-view.md), [deployment](architecture/deployment.md), [crosscutting](architecture/crosscutting.md), [data-flows](architecture/data-flows.md), [scaling](architecture/scaling.md), [quality](architecture/quality.md), [C4 DSL](architecture/c4/workspace.dsl) |
| 0.4 | Пакет ADR | утверждён | [реестр ADR](adr/README.md): 17 приняты, 10 предложены (принимаются в начале своих шагов) |
| 0.5 | Контракты v1 | утверждён | [contracts/](../contracts/README.md): proto, OpenAPI, AsyncAPI, реестр топиков, таксономия, каталог ошибок; проверка `task contracts:check`; ADR-028 (ClickHouse), ADR-029 |
| 0.6 | Модель угроз и требования безопасности | утверждён | [threat-model](security/threat-model.md), [реестр угроз и требований](security/threat-register.md) (источник - [threat-model.yaml](security/threat-model.yaml)); проверка `task docs:threats` |
| 0.7 | Стратегия качества и верификации | утверждён | [strategy](quality/strategy.md), [матрица верификации](quality/verification-matrix.md) (источник - [verification.yaml](quality/verification.yaml)), [сверка](quality/reconciliation.md), [замер задержек](quality/latency-measurement.md), ADR-031; проверка `task docs:verification` |

**Веха M0** (документы Ф0 утверждены, контракты v1 опубликованы, ADR приняты): пройдена. Решения со статусом «Предложено» принимаются в начале своих шагов (реестр ADR).

## Ф1. Инженерная платформа

| Шаг | Название | Статус | Артефакты |
|---|---|---|---|
| 1.1 | Монорепозиторий и toolchain | утверждён | [ADR-032](adr/0032-cpp-toolchain.md), `tools/toolchain`, `CMakePresets.json`, `conanfile.py`, `task cpp:*` |
| 1.2 | Контроль качества кода | утверждён | [ADR-033](adr/0033-code-quality-gates.md), [стандарт C++](engineering/cpp-coding-standard.md), [стандарт TypeScript](engineering/typescript-coding-standard.md), `.clang-format`, `.clang-tidy`, `tools/arch`, `tools/git-hooks`; проверки `task cpp:format`, `cpp:lint`, `cpp:test:sanitizers`, `cpp:coverage`, `arch:check`; барьер - `task check` |
| 1.3 | CI/CD и цепочка поставки | утверждён | [ADR-034](adr/0034-local-ci-and-supply-chain.md); конвейеры `task ci` (перед каждым коммитом, [ADR-036](adr/0036-no-git-hooks.md)), `task ci:nightly`, `task release`; пакеты DEB, образы distroless без root, SBOM CycloneDX, Trivy, подпись Ed25519; барьер бенчмарков `bench:compare` |
| 1.4 | Локальное окружение | утверждён | [ADR-035](adr/0035-local-environment.md); `deploy/local/` (Compose, 1 или 3 брокера Kafka), `task env:up`, `env:test`, `env:ci-test` (этап `env` конвейера); realm Keycloak с политикой SR-02; запуск с нуля около 95 с |
| 1.5 | Веб-монорепозиторий | готов | [ADR-037](adr/0037-web-monorepo.md); `web/`: консоль (Vite, React 19, TanStack, Zustand, Tailwind 4), дизайн-система, API-клиент из OpenAPI, realtime-клиент из proto; `task web:check` (ESLint, типы, Vitest ≥ 80%, бюджеты size-limit), `task web:e2e` (Playwright); этап `web` конвейера |
| 1.6 | Документация как код | не начат | |

## Ф2-Ф11

Не начаты.

## Открытые решения владельца

См. раздел 11 [product brief](product/product-brief.md). Q1 закрыт 2026-10-06: один инженер и ИИ-агент, сокращения объёма приняты. 2026-10-06: решение использовать ClickHouse для истории событий и аналитики (ADR-028). Открыты Q2-Q4.
