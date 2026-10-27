# Представление времени выполнения

<!-- language-links -->
[English](../../architecture/runtime-view.md) | [Русский](runtime-view.md)
<!-- /language-links -->

Раздел 6 arc42. Ключевые сценарии взаимодействия компонентов. Номера этапов B1-B9 - из [бюджета задержек](quality.md#4-бюджет-задержек).

---

## R1. Событие -> инцидент в консоли (S3-S6)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Коннектор (SDK)" as SDK
    participant "Connector Gateway" as GW
    participant "Kafka" as K
    participant "Normalizer" as N
    participant "Correlation Engine" as CE
    participant "Incident Service" as IS
    participant "PostgreSQL" as DB
    participant "API Gateway (realtime)" as API
    participant "Консоль" as UI

    SDK->>SDK: событие: event_id, source_seq -> буфер на диске
    SDK->>GW: EventBatch (кредиты)
    GW->>K: produce psim.ingest.raw.v1 (acks=all)
    K-->>GW: ack
    GW-->>SDK: BatchAck -> удалить из буфера
    K->>N: raw (партиция по source_id)
    N->>N: дедупликация, отображение, обогащение из копии каталога
    N->>K: txn: normalized + state + offsets
    K->>CE: normalized (партиция по site:zone)
    CE->>CE: правила, состояние RocksDB
    CE->>K: txn: signal + changelog + offsets
    K->>IS: signal (партиция по grouping_key)
    IS->>DB: txn: inbox + incident + outbox
    IS->>K: outbox relay -> IncidentCreated
    K->>API: IncidentCreated (все партиции)
    API->>API: фильтр ABAC по site/zone
    API->>UI: WebSocket delta
    UI->>UI: отрисовка, звук
@enduml
```

## R2. Принятие инцидента и сценарий (S7)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Консоль" as UI
    participant "API Gateway" as API
    participant "Incident Service" as IS
    participant "Kafka" as K
    participant "Response Engine" as RE

    note over RE: по IncidentCreated: выбрать сценарий, ResponseRunStarted,\nMandatoryStepsChanged -> Incident.blocking_steps
    UI->>API: POST /v1/incidents/{id}:acknowledge (If-Match: version)
    API->>API: JWT, ABAC
    API->>IS: acknowledge
    IS->>IS: инварианты I1, I5, outbox IncidentAcknowledged
    IS-->>UI: 200 + новая version
    UI->>API: POST /v1/runs/{run}/steps/{step}:complete
    API->>RE: complete step
    RE->>RE: RR2, переход графа, outbox StepCompleted и StepActivated
    K-->>UI: дельты через realtime (Kafka -> API Gateway -> Console)
    UI->>API: POST /v1/incidents/{id}:resolve
    API->>IS: resolve
    IS-->>UI: 409 INCIDENT_MANDATORY_STEPS_PENDING (если blocking_steps не пуст)
@enduml
```

`blocking_steps` обновляется в Incident Service по событиям Response в конечном счёте. Чтобы исключить решение инцидента, пока событие о новом обязательном шаге ещё в пути, Response Engine публикует `MandatoryStepsChanged` в той же транзакции, что и активацию шага; Incident Service при `resolve` дополнительно проверяет, что применил события запуска до версии, известной клиенту (клиент передаёт её в запросе).

## R3. Команда на устройство (S8)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Консоль" as UI
    participant "Command Service" as CS
    participant "Redis (сессии)" as R
    participant "Kafka" as K
    participant "Connector Gateway (все экземпляры)" as GW
    participant "Коннектор" as SDK

    UI->>CS: POST /v1/commands (Idempotency-Key = command_id)
    CS->>CS: CM1, CM2, критичная -> awaiting_approval
    CS->>R: сессия коннектора есть?
    alt нет сессии
        CS-->>UI: failed COMMAND_CONNECTOR_OFFLINE
    else есть
        CS->>K: outbox -> psim.commands.v1 (key connector_id), state sent
        K->>GW: команда
        GW->>SDK: Command (только экземпляр с сессией)
        SDK->>SDK: исполнить не более одного раза по command_id
        SDK-->>GW: CommandAck, затем CommandResult
        GW->>K: psim.commands.results.v1
        K->>CS: результат -> acknowledged / executed / failed
        CS->>K: outbox -> psim.response.events.v1
    end
    note over CS: таймер deadline_at -> timed_out
@enduml
```

## R4. Эскалация по SLA (S9)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Kafka" as K
    participant "Response Engine (таймеры)" as RE
    participant "Incident Service" as IS
    participant "Notification Service" as NS

    K->>RE: IncidentCreated (P2)
    RE->>RE: взвести таймер ack = created_at + 60 с (PostgreSQL)
    note over RE: опрос таймеров каждые 200 мс, блокировка агрегата PostgreSQL
    RE->>RE: таймер сработал, инцидент не принят
    RE->>K: outbox SlaBreached, EscalationLevelTriggered(1)
    K->>IS: ApplyEscalation -> escalation_level = 1, IncidentEscalated
    K->>NS: уведомить получателей уровня 1
    NS->>NS: notification_id детерминирован, каналы in-app и email
    NS->>K: NotificationSent / Delivered -> аудит
@enduml
```

## R5. Переподключение коннектора после обрыва (S11)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Коннектор (SDK)" as SDK
    participant "Gateway-1" as GW1
    participant "Gateway-2" as GW2
    participant "Redis" as R
    participant "Kafka" as K

    SDK->>GW1: EventBatch #41 (seq 1000-1099)
    GW1->>K: produce
    note over GW1: GW1 падает до BatchAck
    SDK->>SDK: связь потеряна, события копятся в буфере
    SDK->>GW2: connect (mTLS) + Register(last_acked: 999)
    GW2->>R: сессия коннектора -> GW2 (вытеснить старую)
    SDK->>GW2: EventBatch seq 1000-…
    GW2->>K: produce (seq 1000-1099 - дубликаты)
    note over K: Normalizer отбрасывает seq 1000-1099 по трекеру
@enduml
```

## R6. Активация набора правил (S4)

```plantuml
@startuml
skinparam backgroundColor white
skinparam defaultFontName SansSerif
skinparam defaultFontSize 13
skinparam shadowing false
skinparam roundcorner 6
skinparam ArrowColor #475569
skinparam packageStyle rectangle
skinparam nodesep 35
skinparam ranksep 45
skinparam maxMessageSize 240
    autonumber
    participant "Администрирование" as UI
    participant "Rule Management (Correlation Engine)" as RM
    participant "Kafka" as K
    participant "Экземпляры Correlation Engine" as CE

    UI->>RM: POST /v1/rules/{id}/versions (черновик)
    RM->>RM: валидация (R1, R3, R4)
    UI->>RM: POST /v1/rules/{id}:dry-run {from, to}
    RM-->>UI: сигналы, которые правило породило бы
    UI->>RM: POST /v1/rule-sets:activate
    RM->>K: psim.config.v1 ruleset/<tenant> (версия N+1)
    K->>CE: все экземпляры читают изменение
    CE->>CE: между пакетами: компиляция, сохранение состояния неизменённых правил, переключение
@enduml
```

## R7. Отказ экземпляра Correlation Engine (S11, QS-A1)

1. При `group.protocol=consumer` брокер исключает неотвечающего участника через `group.consumer.session.timeout.ms` (45 с по умолчанию, 10 с в локальном профиле). Классический fallback использует `session.timeout.ms`.
2. Consumer protocol постепенно перераспределяет партиции; классический fallback использует кооперативную ребалансировку.
3. Новый владелец инициализирует транзакционный продюсер с тем же `transactional.id` - незавершённые транзакции старого владельца прерываются брокером.
4. Состояние партиции восстанавливается из `psim.correlation.changelog.v1` в локальный RocksDB. Цель восстановления ≤ 30 с требует квалификации при объявленных нагрузке и модели отказов.
5. Чтение продолжается с зафиксированного смещения; сигналы, опубликованные в прерванной транзакции, невидимы для `read_committed`-потребителей и будут опубликованы повторно с теми же `signal_id`.

## Аудит и независимая проверка

Производители публикуют действия через outbox в `psim.audit.v1`; Audit Service читает доступную историю с начала retention. Keycloak импортируется read-only клиентом с долговечным курсором. Два экземпляра сериализуют добавление блокировкой PostgreSQL на тенант. Запись содержит непрерывный номер, предыдущий хэш и хэш детерминированного Protobuf.

Периодическая контрольная точка подписывается Ed25519 и синхронно записывается во внешний архив, затем её копия и Kafka outbox фиксируются транзакцией БД. Проверка пересчитывает записи и сверяет архив независимо от таблицы контрольных точек. Экспорт фиксирует свою вершину и содержит всю цепочку; `psim-audit-tool` проверяет её без БД и сети по отдельно доверенным ключам. [Руководство](../engineering/audit-service.md) описывает права, границы защиты и восстановление.

Диаграммы описывают границы протоколов. Для физического действия устройства нет гарантии exactly once; неизвестный исход команды необходимо согласовать до выдачи новой команды. См. [обработку команд](../engineering/command-service.md) и [готовность к выпуску](../product/release-readiness.md).
