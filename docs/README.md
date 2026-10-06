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
