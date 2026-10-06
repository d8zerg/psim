# Контракты PSIM Platform v1

Шаг плана 0.5. Публичные версионируемые контракты платформы - единственный источник истины для кода, генерации клиентов и документации (ADR-001, ADR-004, ADR-026).

## Состав

| Каталог | Что | Источник истины или генерируется |
|---|---|---|
| [proto/psim/common/v1](proto/psim/common/v1) | Конверт сообщения, общие перечисления (ADR-005) | источник |
| [proto/psim/ingest/v1](proto/psim/ingest/v1) | Сырое событие, топик `psim.ingest.raw.v1` | источник |
| [proto/psim/processing/v1](proto/psim/processing/v1) | Нормализованное событие, сигнал, правила корреляции и отображения, внутренние топики | источник |
| [proto/psim/catalog/v1](proto/psim/catalog/v1) | Локации, типы устройств, устройства, источники, коннекторы | источник |
| [proto/psim/incident/v1](proto/psim/incident/v1) | Инцидент, тип инцидента, хронология, события инцидентов | источник |
| [proto/psim/response/v1](proto/psim/response/v1) | Сценарии, запуски, команды, эскалации, уведомления, их события и топики команд | источник |
| [proto/psim/config/v1](proto/psim/config/v1) | Записи конфигурационного топика | источник |
| [proto/psim/audit/v1](proto/psim/audit/v1) | Записи аудита и контрольные точки | источник |
| [proto/psim/connector/v1](proto/psim/connector/v1) | Протокол коннекторов, gRPC (ADR-017) | источник |
| [proto/psim/realtime/v1](proto/psim/realtime/v1) | Протокол realtime-канала, WebSocket (ADR-019) | источник |
| [proto/psim/api/v1](proto/psim/api/v1) | REST API v1: сервисы с `google.api.http` (ADR-026) | источник |
| [openapi/overlay.yaml](openapi/overlay.yaml) | То, что не выражается аннотациями: безопасность, `If-Match`, `Idempotency-Key`, RFC 9457, бинарные тела | источник |
| [openapi/psim-api-v1.yaml](openapi/psim-api-v1.yaml) | OpenAPI 3 REST API v1 | генерируется |
| [topics/topics.yaml](topics/topics.yaml) | Реестр топиков Kafka: ключи, партиции, retention, семантика, производители и потребители | источник |
| [asyncapi/psim-topics.yaml](asyncapi/psim-topics.yaml) | AsyncAPI 3 топиков Kafka | генерируется |
| [taxonomy/v1/taxonomy.yaml](taxonomy/v1/taxonomy.yaml) | Таксономия событий v1 | источник |
| [errors/errors.yaml](errors/errors.yaml) | Каталог кодов ошибок | источник |
| [docs/proto-reference.md](docs/proto-reference.md) | Справочник по всем сообщениям и сервисам | генерируется |

Сгенерированные файлы хранятся в репозитории, чтобы их можно было читать и ревьюить, но правятся только через источники.

## Соглашения

- Пакеты `psim.<context>.v<N>`, файлы `proto/psim/<context>/v<N>/*.proto`; линтер buf `STANDARD` + `COMMENTS`.
- Значение сообщения в топике - запись `<...>Record { Envelope envelope = 1; oneof payload {...} }`; суффикс `Record` зарезервирован за записями топиков.
- События агрегатов несут состояние агрегата после изменения; `envelope.sequence` = версия агрегата. Потребители применяют событие, только если версия больше применённой.
- Идентификаторы - строки UUID; правила построения - ADR-005 и ADR-029.
- Ограничения полей - аннотации protovalidate (`buf.validate`); сервисы проверяют входящие сообщения теми же правилами.
- REST: каноническое JSON-отображение Protobuf, курсорная пагинация, `If-Match` для изменений версионированных ресурсов, `Idempotency-Key` для неидемпотентных операций, ошибки `application/problem+json`.

## Правила эволюции (ADR-004)

1. Только добавление необязательных полей с новыми номерами; удалённые поля и номера - в `reserved`.
2. Тип и смысл поля не меняются; новый смысл - новое поле.
3. Новый тип события - новая ветвь `oneof`; потребители игнорируют неизвестные ветви.
4. Несовместимое изменение - новый пакет `v2` и новый топик `.v2`.
5. Таксономия и каталог ошибок - только добавление; коды не переименовываются и не переиспользуются.

Автоматизация: `buf breaking` (правила `WIRE_JSON`) против ветки `master`; в Schema Registry - режимы совместимости из ADR-004 (шаг 2.3).

## Проверка

Все действия выполняются задачами [Task](https://taskfile.dev) ([ADR-030](../docs/adr/0030-task-runner.md)):

```
task contracts:check     # проверить всё; падает, если сгенерированные файлы устарели
task contracts:gen       # перегенерировать OpenAPI, AsyncAPI, справочник и проверить согласованность
task contracts:lint      # отдельные шаги: format, format:fix, lint, breaking, build, openapi, asyncapi, reference, verify
task contracts:buf -- dep update   # произвольная команда buf
```

Что проверяется:

| Проверка | Инструмент |
|---|---|
| Форматирование, линтер, сборка proto | buf 1.73 |
| Обратная совместимость против `master` | `buf breaking` |
| OpenAPI: генерация, оверлей, валидность | protoc-gen-openapi, `tools/contracts/openapi_overlay.py`, Redocly CLI |
| AsyncAPI: генерация из реестра, валидность | `tools/contracts/contracts.py`, AsyncAPI CLI |
| Справочник proto актуален | protoc-gen-doc |
| Реестр топиков: записи существуют в proto, имеют конверт и `oneof payload`; каждая запись привязана к топику; совпадение с таблицей в [data-flows.md](../docs/architecture/data-flows.md#4-сводная-таблица) | `contracts.py verify` |
| Таксономия: уникальность, формат, классы, критичность, ссылки на восстановление; совпадение с [event-taxonomy.md](../docs/domain/event-taxonomy.md) | `contracts.py verify` |
| Каталог ошибок: формат, HTTP и gRPC статусы; все коды, упомянутые в документации и proto, есть в каталоге | `contracts.py verify` |
| Схема `Problem` в оверлее совпадает с `psim.api.v1.Problem` | `contracts.py verify` |

Инструменты запускаются в контейнерах с закреплёнными версиями; нужны Docker, Python 3 с PyYAML и Task. buf и плагины `protoc-gen-openapi`, `protoc-gen-doc` собраны в локальный образ ([tools/contracts/image](../tools/contracts/image/Dockerfile), задача `task tools:build`), поэтому проверки не зависят от лимитов Buf Schema Registry и после первой загрузки зависимостей работают без сети. В CI (шаг 1.3) `task check` становится обязательным барьером слияния.

## Покрытие сценариев приёмки

Каждый сценарий S1-S13 ([acceptance-spec.md](../docs/product/acceptance-spec.md)) выражается через контракты:

| Сценарий | Контракты |
|---|---|
| S1 Подключение коннектора | `CatalogService.RegisterConnector`; `ConnectorGatewayService.Session` (`Register`, `Registered`, `EventBatch`, `BatchAck`, `ConfigUpdate`); `MappingService.PublishMappingRuleSet`; `CatalogService.ListConnectors` (`ConnectorStatus.online`); топики `psim.ingest.raw.v1`, `psim.events.normalized.v1`, `psim.dlq.normalizer.v1`; коды `AUTH_UNAUTHENTICATED`, `INGEST_UNKNOWN_CONNECTOR`, `INGEST_INVALID_EVENT`, `PROCESSING_NO_MAPPING` |
| S2 Описание объекта | `CatalogService`: `CreateLocation`, `UploadFloorPlan`, `StartImport`, `GetImport` (`ImportJob.errors`), `UpdateDevice` (`position`); топик `psim.catalog.v1`; коды `CATALOG_DUPLICATE_EXTERNAL_ID`, `CATALOG_IMPORT_ROW_INVALID` |
| S3 Нормализация | `RawEvent` -> `Event` (тип таксономии, `severity`, `site_id`, `zone_id`, `device_id`); `Envelope.sequence`, `traceparent`; `SourceSequenceState` (дедупликация); коды `PROCESSING_DEVICE_NOT_FOUND`, `PROCESSING_DEVICE_DISABLED` |
| S4 Корреляция | `CorrelationRule` (`ConjunctionSpec`, `window`, `correlation_scope`); `CorrelationRuleService`: `SaveRuleDraft`, `ValidateRule`, `StartDryRun`, `ActivateRuleSet`; `ConfigRecord.rule_set`; `Signal` (`event_ids`, `origin`); код `PROCESSING_RULE_INVALID` |
| S5 Инцидент без дубликатов | `Signal.grouping_key`, детерминированный `Envelope.message_id` сигнала; `IncidentCreated`, `SignalAttached` |
| S6 Инцидент в консоли | `FeedService.GetIncidentFeed` (`resume_token`); realtime `Subscribe`, `Delta`; `Envelope.occurred_at` для замера задержки; код `AUTH_SCOPE_DENIED` |
| S7 Сценарий | `IncidentService.AcknowledgeIncident`, `ResolveIncident` (`known_response_run_version`); `ResponsePlan` (`ChecklistParams`, `DecisionParams`, `Step.mandatory`); `ResponseRunService.CompleteStep`; `RunUpdated`; код `INCIDENT_MANDATORY_STEPS_PENDING` |
| S8 Команда | `CommandService.RequestCommand`, `ApproveCommand`; `CommandDeliveryRecord`; протокол `Command`, `CommandAck`, `CommandResult`; `CommandUpdated`; коды `COMMAND_APPROVAL_SELF`, `COMMAND_CONNECTOR_OFFLINE`, `COMMAND_TIMED_OUT`, `COMMAND_DEVICE_REJECTED` |
| S9 Эскалация | `EscalationPolicy` (`SlaTarget`, `EscalationLevel`); `SlaBreached`, `EscalationLevelTriggered`, `IncidentEscalated`; `NotificationUpdated`; `NotificationService.ListMyNotifications` |
| S10 Аудит | `AuditEventRecord` (`AuditEntry`, `AuditCheckpoint`); `AuditService`: `SearchAuditRecords`, `VerifyAuditIntegrity` (`first_broken_seq`), `ExportAuditRecords`; `AuditOutcome.AUDIT_OUTCOME_DENIED` |
| S11 Отказ без потерь | Семантика топиков в [topics.yaml](topics/topics.yaml); `ConnectorEvent.event_id`, `source_seq`; `BatchAck`; realtime `resume_token`, `ResyncRequired`; код `REALTIME_RESYNC_REQUIRED` |
| S12 Обновление | Правила эволюции и `buf breaking`; согласование версии в `Register.protocol_versions`; `Envelope.schema_version` |
| S13 Отчёт | `ReportService.GetIncidentReport` (`IncidentMetrics`: `mtta`, `mttr`, `false_alarm_rate`, соблюдение SLA), `ExportIncidentReport` (CSV) |
