# Представление времени выполнения

Версия 0.1 · Шаг плана 0.3 · Статус: черновик

Раздел 6 arc42. Ключевые сценарии взаимодействия компонентов. Номера этапов B1-B9 - из [бюджета задержек](quality.md#4-бюджет-задержек).

---

## R1. Событие -> инцидент в консоли (S3-S6)

```mermaid
sequenceDiagram
    autonumber
    participant SDK as Коннектор (SDK)
    participant GW as Connector Gateway
    participant K as Kafka
    participant N as Normalizer
    participant CE as Correlation Engine
    participant IS as Incident Service
    participant DB as PostgreSQL
    participant API as API Gateway (realtime)
    participant UI as Консоль

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
```

## R2. Принятие инцидента и сценарий (S7)

```mermaid
sequenceDiagram
    autonumber
    participant UI as Консоль
    participant API as API Gateway
    participant IS as Incident Service
    participant K as Kafka
    participant RE as Response Engine

    Note over RE: по IncidentCreated: выбрать сценарий, ResponseRunStarted,<br/>MandatoryStepsChanged -> Incident.blocking_steps
    UI->>API: POST /v1/incidents/{id}:acknowledge (If-Match: version)
    API->>API: JWT, ABAC
    API->>IS: acknowledge
    IS->>IS: инварианты I1, I5, outbox IncidentAcknowledged
    IS-->>UI: 200 + новая version
    UI->>API: POST /v1/runs/{run}/steps/{step}:complete
    API->>RE: complete step
    RE->>RE: RR2, переход графа, outbox StepCompleted и StepActivated
    K-->>UI: дельты через realtime (R1, шаги 13-16)
    UI->>API: POST /v1/incidents/{id}:resolve
    API->>IS: resolve
    IS-->>UI: 409 INCIDENT_MANDATORY_STEPS_PENDING (если blocking_steps не пуст)
```

`blocking_steps` обновляется в Incident Service по событиям Response в конечном счёте. Чтобы исключить решение инцидента, пока событие о новом обязательном шаге ещё в пути, Response Engine публикует `MandatoryStepsChanged` в той же транзакции, что и активацию шага; Incident Service при `resolve` дополнительно проверяет, что применил события запуска до версии, известной клиенту (клиент передаёт её в запросе).

## R3. Команда на устройство (S8)

```mermaid
sequenceDiagram
    autonumber
    participant UI as Консоль
    participant CS as Command Service
    participant R as Redis (сессии)
    participant K as Kafka
    participant GW as Connector Gateway (все экземпляры)
    participant SDK as Коннектор

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
    Note over CS: таймер deadline_at -> timed_out
```

## R4. Эскалация по SLA (S9)

```mermaid
sequenceDiagram
    autonumber
    participant K as Kafka
    participant RE as Response Engine (таймеры)
    participant IS as Incident Service
    participant NS as Notification Service

    K->>RE: IncidentCreated (P2)
    RE->>RE: взвести таймер ack = created_at + 60 с (PostgreSQL)
    Note over RE: опрос таймеров каждые 200 мс экземпляром-владельцем партиции
    RE->>RE: таймер сработал, инцидент не принят
    RE->>K: outbox SlaBreached, EscalationLevelTriggered(1)
    K->>IS: ApplyEscalation -> escalation_level = 1, IncidentEscalated
    K->>NS: уведомить получателей уровня 1
    NS->>NS: notification_id детерминирован, каналы in-app и email
    NS->>K: NotificationSent / Delivered -> аудит
```

## R5. Переподключение коннектора после обрыва (S11)

```mermaid
sequenceDiagram
    autonumber
    participant SDK as Коннектор (SDK)
    participant GW1 as Gateway-1
    participant GW2 as Gateway-2
    participant R as Redis
    participant K as Kafka

    SDK->>GW1: EventBatch #41 (seq 1000-1099)
    GW1->>K: produce
    Note over GW1: GW1 падает до BatchAck
    SDK->>SDK: связь потеряна, события копятся в буфере
    SDK->>GW2: connect (mTLS) + Register(last_acked: 999)
    GW2->>R: сессия коннектора -> GW2 (вытеснить старую)
    SDK->>GW2: EventBatch seq 1000-…
    GW2->>K: produce (seq 1000-1099 - дубликаты)
    Note over K: Normalizer отбрасывает seq 1000-1099 по трекеру
```

## R6. Активация набора правил (S4)

```mermaid
sequenceDiagram
    autonumber
    participant UI as Администрирование
    participant RM as Rule Management (Correlation Engine)
    participant K as Kafka
    participant CE as Экземпляры Correlation Engine

    UI->>RM: POST /v1/rules/{id}/versions (черновик)
    RM->>RM: валидация (R1, R3, R4)
    UI->>RM: POST /v1/rules/{id}:dry-run {from, to}
    RM-->>UI: сигналы, которые правило породило бы
    UI->>RM: POST /v1/rulesets:activate
    RM->>K: psim.config.v1 ruleset/<tenant> (версия N+1)
    K->>CE: все экземпляры читают изменение
    CE->>CE: между пакетами: компиляция, сохранение состояния неизменённых правил, переключение
```

## R7. Отказ экземпляра Correlation Engine (S11, QS-A1)

1. Экземпляр перестаёт отвечать; брокер исключает его из группы через `session.timeout.ms` (10 с).
2. Кооперативная ребалансировка передаёт его партиции оставшимся экземплярам.
3. Новый владелец инициализирует транзакционный продюсер с тем же `transactional.id` - незавершённые транзакции старого владельца прерываются брокером.
4. Состояние партиции восстанавливается из `psim.correlation.changelog.v1` в локальный RocksDB (≤ 30 с).
5. Чтение продолжается с зафиксированного смещения; сигналы, опубликованные в прерванной транзакции, невидимы для `read_committed`-потребителей и будут опубликованы повторно с теми же `signal_id`.
