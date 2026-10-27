# Реестр архитектурных решений (ADR)

<!-- language-links -->
[English](../../adr/README.md) | [Русский](README.md)
<!-- /language-links -->

Формат - [шаблон](template.md) на основе MADR.

## Статусы

| Статус | Смысл |
|---|---|
| Предложено | Решение сформулировано, но ещё не принято: его подтверждают перед реализацией с учётом замеров и прототипов |
| Принято | Решение действует; реализация обязана ему следовать |
| Заменено ADR-NNN | Решение отменено более поздним ADR |
| Отклонено | Рассмотрено и не принято |

Правило: ADR принимается до реализации зависящих от него изменений. Изменение принятого решения оформляется новым ADR, который заменяет старый; старый не редактируется по существу.

## Реестр

| № | Решение | Статус |
|---|---|---|
| [ADR-001](0001-monorepo-structure.md) | Структура монорепозитория и границы модулей | Принято |
| [ADR-002](0002-kafka-central-bus.md) | Kafka как центральная шина; дистрибутив и версия | Принято |
| [ADR-003](0003-schema-registry.md) | Schema Registry | Принято |
| [ADR-004](0004-serialization-and-schema-evolution.md) | Формат сериализации и правила эволюции схем | Принято |
| [ADR-005](0005-envelope-and-identifiers.md) | Конверт сообщения, UUIDv7 и именные UUID | Принято |
| [ADR-006](0006-delivery-semantics.md) | Семантика доставки по потокам | Принято |
| [ADR-007](0007-partitioning-strategy.md) | Стратегия партиционирования и обработка «горячих» ключей | Принято |
| [ADR-008](0008-cqrs-outbox-inbox.md) | CQRS, transactional outbox и inbox | Принято |
| [ADR-009](0009-correlation-state-store.md) | Хранение состояния корреляции: RocksDB с changelog-топиком | Принято |
| [ADR-010](0010-time-semantics.md) | Семантика времени: event time, водяные знаки, рассогласование часов | Принято |
| [ADR-011](0011-correlation-rule-language.md) | Язык правил корреляции | Предложено |
| [ADR-012](0012-response-execution-and-timers.md) | Исполнение сценариев реагирования и сервис таймеров | Принято |
| [ADR-013](0013-event-history-storage.md) | Хранение истории событий в MVP и долговременный архив | Заменено ADR-028 |
| [ADR-014](0014-postgresql-async-driver.md) | Асинхронный драйвер PostgreSQL | Принято |
| [ADR-015](0015-redis-client-and-role.md) | Redis-совместимое хранилище, клиент и его роль | Принято |
| [ADR-016](0016-threading-model.md) | Модель потоков исполнения и корутины | Принято |
| [ADR-017](0017-connector-protocol.md) | Протокол коннекторов и управление потоком | Принято |
| [ADR-018](0018-command-routing.md) | Маршрутизация команд к сессиям коннекторов | Принято |
| [ADR-019](0019-realtime-channel.md) | Realtime-канал: WebSocket, снимок и дельты, возобновление по смещениям | Принято |
| [ADR-020](0020-authorization-model.md) | Модель авторизации: RBAC + ABAC, точка принятия решений | Принято |
| [ADR-021](0021-audit-hash-chain.md) | Неизменяемый аудит на цепочке хэшей | Принято |
| [ADR-022](0022-multitenancy.md) | Мультитенантность в модели данных при одном тенанте в MVP | Принято |
| [ADR-023](0023-deployment-topologies.md) | Топологии развёртывания и границы надёжности режима «одна машина» | Принято |
| [ADR-024](0024-zero-downtime-upgrades.md) | Обновления без простоя и миграции | Принято |
| [ADR-025](0025-ai-extension-points.md) | Точки расширения для ИИ | Принято |
| [ADR-026](0026-rest-contract-and-dto.md) | REST-контракт и DTO: proto-first с генерацией OpenAPI | Принято |
| [ADR-027](0027-incident-states.md) | Эскалация и «ложная тревога» - не состояния инцидента | Принято |
| [ADR-028](0028-clickhouse-analytical-storage.md) | ClickHouse для истории событий и аналитики | Принято |
| [ADR-029](0029-deterministic-source-id.md) | Детерминированный идентификатор источника | Принято |
| [ADR-030](0030-task-runner.md) | Task как единая точка запуска задач разработки | Принято |
| [ADR-031](0031-verification-tooling.md) | Инструменты верификации | Принято |
| [ADR-032](0032-cpp-toolchain.md) | C++ toolchain: Debian 13 x86_64, clang 21 и libstdc++, Conan 2, CMake пресеты | Принято |
| [ADR-033](0033-code-quality-gates.md) | Контроль качества кода: форматирование, clang-tidy, контроль include, санитайзеры, покрытие, fitness functions | Принято |
| [ADR-034](0034-local-ci-and-supply-chain.md) | Локальный конвейер CI/CD и цепочка поставки | Принято |
| [ADR-035](0035-local-environment.md) | Локальное окружение разработки и интеграционного тестирования | Принято |
| [ADR-036](0036-no-git-hooks.md) | Отказ от git-хуков: `task ci` перед коммитом | Принято |
| [ADR-037](0037-web-monorepo.md) | Веб-монорепозиторий: инструменты, генерация клиентов, барьеры | Принято |
| [ADR-038](0038-documentation-portal.md) | Портал документации из репозитория | Принято |
| [ADR-039](0039-service-runtime.md) | Runtime сервиса | Принято |
| [ADR-040](0040-async-primitives.md) | Асинхронные примитивы платформы | Принято |
| [ADR-041](0041-kafka-layer.md) | Слой Kafka | Принято |
| [ADR-042](0042-operational-capabilities.md) | Эксплуатационные функции в границах MVP | Принято |
| [ADR-043](0043-platform-security.md) | Безопасность платформы: TLS, JWT/JWKS, секреты и внешняя PKI | Принято |
| [ADR-044](0044-deterministic-simulation.md) | Детерминированный симуляционный стенд с ручным запуском | Принято |
| [ADR-045](0045-clickhouse-layer.md) | Нативный слой ClickHouse и восстановление пакетов Kafka | Принято |
| [ADR-046](0046-s3-object-storage.md) | S3-совместимое файловое хранилище | Принято |
| [ADR-047](0047-event-flow.md) | Сквозной минимальный поток | Принято |
| [ADR-048](0048-connector-sdk.md) | Connector SDK, дисковый буфер и сверка состояния | Принято |
| [ADR-049](0049-connector-gateway.md) | Connector Gateway: регистрации, сессии и подтверждение отзыва | Принято |
| [ADR-050](0050-reference-connectors.md) | Эталонные коннекторы и долговечные HTTP-повторы | Принято |
| [ADR-051](0051-resource-catalog.md) | Resource Catalog: ресурсы, файлы и обслуживание | Принято |
| [ADR-052](0052-normalization-and-event-history.md) | Нормализация, курсоры источников и история событий | Принято |
| [ADR-053](0053-incident-service.md) | Incident Service | Принято |
| [ADR-054](0054-event-history-links.md) | История событий, связи и продление хранения | Принято |
| [ADR-055](0055-command-service.md) | Сервис команд устройствам | Принято |
| [ADR-056](0056-sla-and-notifications.md) | SLA, дежурства и доставка уведомлений | Принято |
| [ADR-057](0057-incident-reporting.md) | Факты инцидентов и отчётность | Принято |
| [ADR-058](0058-api-gateway-and-configuration.md) | API Gateway и управление конфигурацией нормализации | Принято |
| [ADR-059](0059-console-projections.md) | Read-модели консоли в Projection Service | Принято |
| [ADR-060](0060-realtime-delivery.md) | Возобновление realtime из Kafka и оперативные снимки | Принято |
| [ADR-061](0061-live-access-validation.md) | Проверка актуальности токенов и наборов областей | Принято |
| [ADR-062](0062-design-system-and-stable-updates.md) | Дизайн-система и стабильное обновление интерфейса | Принято |
| [ADR-063](0063-interface-principles.md) | Принципы оперативного интерфейса | Принято |
| [ADR-064](0064-alarm-philosophy.md) | Философия тревог и нагрузка оператора | Принято |
| [ADR-065](0065-ui-loading-budgets.md) | Загрузка и бюджеты интерфейса | Принято |
| [ADR-066](0066-ui-font-and-icons.md) | Локальные шрифты и предметные иконки | Принято |
| [ADR-067](0067-assistant-boundaries.md) | Ассистент с источниками и без выполнения команд | Принято |
| [ADR-068](0068-operational-interface-backend.md) | Серверные операции оперативного интерфейса | Принято |
| [ADR-069](0069-application-shell-and-session-recovery.md) | Оболочка приложения и восстановление сессии | Принято |
| [ADR-070](0070-lazy-loading-and-delivery-budgets.md) | Ленивая загрузка модулей и бюджеты поставки | Принято |
| [ADR-071](0071-operator-console.md) | Рабочая консоль оператора | Принято |
| [ADR-072](0072-administration.md) | Модуль администрирования | Принято |
| [ADR-073](0073-administration-delivery-budget.md) | Бюджет полного графа приложения с администрированием | Принято |
| [ADR-074](0074-audit-and-reporting-interface.md) | Интерфейс аудита и отчётности | Принято |
| [ADR-075](0075-audit-and-reporting-delivery-budget.md) | Бюджет уникальной JS-поставки с аудитом и отчётами | Принято |
| [ADR-076](0076-web-client-qualification.md) | Квалификация веб-клиента | Принято |
| [ADR-077](0077-seaweedfs-deployment-topology.md) | Эксплуатационная топология SeaweedFS | Принято |
| [ADR-078](0078-shared-api-rate-limits.md) | Общие квоты REST API в Valkey | Принято |
| [ADR-079](0079-release-version-identification.md) | Определение версии и идентичности поставки | Принято |
| [ADR-080](0080-observability-package.md) | Хранение телеметрии и пакет наблюдаемости | Принято |
| [ADR-081](0081-administration-and-recovery.md) | Нативные административные операции и резервные копии | Принято |

ADR-042 определяет эксплуатационные требования к файлам, состоянию, обслуживанию, передаче ответственности, сертификатам и replay.
