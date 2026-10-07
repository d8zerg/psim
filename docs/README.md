# Документация PSIM Platform

- [План разработки MVP](psim-mvp-development-plan.md) · [диаграммы](psim-mvp-diagrams.drawio)
- [Прогресс по шагам плана](progress.md)

## Продукт (шаг 0.1)

- [Product Brief](product/product-brief.md)
- [Роли и пользовательские пути](product/roles-and-journeys.md)
- [Границы MVP](product/mvp-scope.md)
- [Спецификация приёмки S1-S13](product/acceptance-spec.md)

## Предметная область (шаг 0.2)

- [Глоссарий](domain/glossary.md)
- [Bounded contexts и карта контекстов](domain/context-map.md)
- [Агрегаты, инварианты, доменные события](domain/aggregates.md)
- [Модели состояний](domain/state-models.md)
- [Таксономия событий v1 и модель критичности](domain/event-taxonomy.md)

## Архитектура (шаг 0.3)

- [Архитектурный документ arc42](architecture/README.md)
- [Представление времени выполнения](architecture/runtime-view.md)
- [Развёртывание и топологии](architecture/deployment.md)
- [Сквозные концепции](architecture/crosscutting.md)
- [Потоки данных и семантика доставки](architecture/data-flows.md)
- [Масштабирование и партиционирование](architecture/scaling.md)
- [Требования к качеству и бюджет задержек](architecture/quality.md)
- [Модель C4 (Structurizr DSL)](architecture/c4/workspace.dsl) - проверка: `task docs:c4`

## Архитектурные решения (шаг 0.4)

- [Реестр ADR](adr/README.md) · [шаблон](adr/template.md)

## Контракты (шаг 0.5)

- [Контракты v1: состав, соглашения, проверка, покрытие сценариев](../contracts/README.md)
- [OpenAPI REST v1](../contracts/openapi/psim-api-v1.yaml) · [AsyncAPI топиков](../contracts/asyncapi/psim-topics.yaml) · [справочник proto](../contracts/docs/proto-reference.md)

## Безопасность (шаг 0.6)

- [Модель угроз: методика, активы, границы доверия, допущения](security/threat-model.md)
- [Реестр угроз и требований безопасности](security/threat-register.md) (генерируется из [threat-model.yaml](security/threat-model.yaml))

## Качество и верификация (шаг 0.7)

- [Стратегия качества и верификации](quality/strategy.md)
- [Матрица верификации](quality/verification-matrix.md) (генерируется из [verification.yaml](quality/verification.yaml))
- [Методика сквозной сверки потерь и дубликатов](quality/reconciliation.md)
- [Методика замера задержек](quality/latency-measurement.md)

## Инженерная платформа (шаги 1.1-1.6)

- [Стандарт кодирования C++23](engineering/cpp-coding-standard.md)
- [Стандарт кодирования TypeScript](engineering/typescript-coding-standard.md)
- Барьеры качества и fitness functions - [ADR-033](adr/0033-code-quality-gates.md); toolchain - [ADR-032](adr/0032-cpp-toolchain.md)
- Локальный конвейер CI/CD, пакеты, образы, SBOM и подписи - [ADR-034](adr/0034-local-ci-and-supply-chain.md)
- Портал документации (`task docs:portal`, `task docs:serve`) - [ADR-038](adr/0038-documentation-portal.md)
- Веб-монорепозиторий (консоль, дизайн-система, клиенты REST и realtime) - [ADR-037](adr/0037-web-monorepo.md), [web/](../web/README.md)
- Локальное окружение (Kafka, Schema Registry, PostgreSQL, ClickHouse, Valkey, Keycloak, наблюдаемость, Toxiproxy) - [ADR-035](adr/0035-local-environment.md), [deploy/](../deploy/README.md)

## Платформенное ядро (Ф2)

- Runtime сервиса: конфигурация, логи, метрики, трассировка, health, остановка с дренированием, шаблон сервиса - [ADR-039](adr/0039-service-runtime.md), [сервисы](../services/README.md)
- Асинхронная модель: шарды, корутины, дедлайны, противодавление, пул блокирующих операций - [ADR-016](adr/0016-threading-model.md), [ADR-040](adr/0040-async-primitives.md)
