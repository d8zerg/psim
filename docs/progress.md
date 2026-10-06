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
| 0.7 | Стратегия качества и верификации | готов | [strategy](quality/strategy.md), [матрица верификации](quality/verification-matrix.md) (источник - [verification.yaml](quality/verification.yaml)), [сверка](quality/reconciliation.md), [замер задержек](quality/latency-measurement.md), ADR-031; проверка `task docs:verification` |

**Веха M0** (документы Ф0 утверждены, контракты v1 опубликованы, ADR приняты): все шаги Ф0 выполнены, ожидается утверждение шага 0.7. Решения со статусом «Предложено» принимаются в начале своих шагов (реестр ADR).

## Ф1-Ф11

Не начаты. Статусы шагов добавляются по мере начала фазы.

## Открытые решения владельца

См. раздел 11 [product brief](product/product-brief.md). Q1 закрыт 2026-10-06: один инженер и ИИ-агент, сокращения объёма приняты. 2026-10-06: решение использовать ClickHouse для истории событий и аналитики (ADR-028). Открыты Q2-Q4.
