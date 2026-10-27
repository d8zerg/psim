# Glossary

<!-- language-links -->
[English](glossary.md) | [Русский](../ru/domain/glossary.md)
<!-- /language-links -->

The project's shared terminology is used consistently in documentation, code, contracts and the interface. Code uses the English identifiers in the Code column.

## General rules

- Entity identifiers are UUIDv7 unless deterministic construction is explicitly specified; see [aggregates.md](aggregates.md), section 0.
- Tenant ownership is represented by `tenant_id` in the domain and message envelopes. HTTP requests derive the tenant from the authenticated principal.
- Time is stored in UTC with microsecond precision. **Occurrence time** (`occurred_at`, measured by the source clock) differs from **receipt time** (`received_at`, measured by the platform clock).

## Terms

| English term | Russian term | Code | Context | Definition |
|---|---|---|---|---|
| Tenant | `Тенант` | `Tenant` | Platform | An isolated organization that owns data. The MVP has one tenant, but tenant identity is included in every key |
| Location | `Локация` | `Location` | Catalog | A node in the protected-site hierarchy: site, building, floor or zone |
| Site | `Площадка` | `Site` | Catalog | A root location representing a geographically distinct property, such as a factory, campus or station |
| Building | `Здание` | `Building` | Catalog | A location within a site |
| Floor | `Этаж` | `Floor` | Catalog | A location within a building; it may have a floor plan |
| Zone | `Зона` | `Zone` | Catalog | The smallest location to which devices and rules are assigned. A zone may belong to a floor, building or directly to a site for outdoor and perimeter areas |
| Floor plan | `Поэтажный план` | `FloorPlan` | Catalog | A floor image with a coordinate system for placing devices |
| Device type | `Тип устройства` | `DeviceType` | Catalog | A device class with a set of sources and capabilities, such as a motion sensor, access-control door or camera |
| Device | `Устройство` | `Device` | Catalog | Physical or logical equipment assigned to a zone and served by one connector |
| Source | `Источник` | `Source` | Catalog | A device event channel, such as a circuit, input or video-analytics detector. A device has one or more sources. The source is the raw-event partition key; its identifier is deterministic ([ADR-029](../adr/0029-deterministic-source-id.md)) |
| Capability | `Возможность` | `Capability` | Catalog | A command or mode supported by a device, such as `lock`, `unlock`, `arm` or `ptz.preset` |
| Connector | `Коннектор` | `Connector` | Catalog / Integration | A software adapter connecting an external system to the platform, registered in Connector Gateway with mTLS credentials and represented in the catalog |
| Connector session | `Сессия коннектора` | `ConnectorSession` | Integration | The lifetime of one connector connection to a gateway |
| Raw event | `Сырое событие` | `RawEvent` | Integration | An event as sent by the connector: source identifier, sequence number, original event code and attributes |
| Source sequence | `Порядковый номер источника` | `source_seq` | Integration | A monotonically increasing event number at the source, used for deduplication and end-to-end reconciliation |
| Mapping rule | `Правило отображения` | `MappingRule` | Event Processing | A declarative rule mapping a raw event to a taxonomy type and severity |
| Event | `Событие` | `Event` | Event Processing | A normalized event with taxonomy type, severity, site, zone, device and source |
| Significant event | `Значимое событие` | `SignificantEvent` | Event Processing | An event whose severity meets the threshold or which is linked to a signal. It is retained in event history longer than other events ([ADR-028](../adr/0028-clickhouse-analytical-storage.md)) |
| Taxonomy | `Таксономия` | `Taxonomy` | Event Processing | The hierarchical event-type catalog; see [event-taxonomy.md](event-taxonomy.md) |
| Severity | `Критичность` | `Severity` | Event Processing | The importance of an individual event: `info`, `low`, `medium`, `high` or `critical` |
| Correlation rule | `Правило корреляции` | `CorrelationRule` | Event Processing | A declarative rule producing a signal from one or more events using a condition, window, sequence, threshold or missing event |
| Rule set | `Набор правил` | `RuleSet` | Event Processing | A versioned collection of active tenant rules applied atomically |
| Correlation key | `Ключ корреляции` | `correlation_key` | Event Processing | The key under which a rule accumulates state, such as a zone. A broader scope uses repartitioning; signals are partitioned by `grouping_key` |
| Window | `Окно` | `Window` | Event Processing | An event-time interval in which a rule matches events |
| Watermark | `Водяной знак` | `Watermark` | Event Processing | An estimate of the event-time point through which all events are considered received |
| Signal | `Сигнал` | `Signal` | Event Processing | A rule activation indicating a situation requiring attention. It contains incident type, priority, grouping key and event references |
| Grouping key | `Ключ группировки` | `grouping_key` | Incident | The key used to combine signals into one open incident |
| Incident type | `Тип инцидента` | `IncidentType` | Incident | A situation category, such as intrusion, fire or equipment failure, that determines the response plan and SLA |
| Incident | `Инцидент` | `Incident` | Incident | A situation requiring operator response; the product's central aggregate |
| Priority | `Приоритет` | `Priority` | Incident | Incident urgency from `P1`, the highest, to `P4` |
| Timeline | `Хронология` | `Timeline` | Incident | An ordered journal of everything that happened to an incident |
| Resolution | `Решение инцидента` | `Resolution` | Incident | The outcome: `confirmed` for a real situation, `false_alarm`, `test` or `duplicate` |
| Escalation | `Эскалация` | `Escalation` | Response | Involvement of the next responsibility level following an SLA breach |
| Escalation policy | `Политика эскалации` | `EscalationPolicy` | Response | SLA limits and escalation levels associated with incident type and priority |
| SLA | `SLA` | `Sla` | Response | The maximum time to acknowledge (`ack`) or resolve (`resolve`) an incident |
| Response plan | `Сценарий реагирования` | `ResponsePlan` | Response | A versioned operating procedure represented as a graph of steps; shown as a response plan or SOP in the interface |
| Step | `Шаг` | `Step` | Response | A response-plan element: instruction, checklist, decision, command, notification, wait or escalation |
| Mandatory step | `Обязательный шаг` | `mandatory` | Response | A step that must be completed before the incident can be resolved |
| Response run | `Запуск сценария` | `ResponseRun` | Response | Execution of a specific plan version for a specific incident |
| Command | `Команда` | `Command` | Response | A request to act on equipment through a connector, such as locking a door or arming a device |
| Critical command | `Критичная команда` | `critical` | Response | A command requiring approval by a second user |
| Notification | `Уведомление` | `Notification` | Response | A message delivered through an in-app, email or webhook channel |
| User | `Пользователь` | `User` | Identity & Access | A person authenticated through Keycloak |
| Role | `Роль` | `Role` | Identity & Access | A permission set: `operator`, `supervisor`, `admin`, `integrator`, `auditor` or `manager` |
| Access scope | `Область доступа` | `AccessScope` | Identity & Access | Sites and zones within which a user's role applies |
| Audit record | `Запись аудита` | `AuditRecord` | Audit | An immutable record of a subject's action on an object, included in a hash chain |
| Audit checkpoint | `Контрольная точка` | `AuditCheckpoint` | Audit | A periodically recorded hash of the audit-chain head |
| Envelope | `Конверт` | `Envelope` | Platform | The common wrapper of every Kafka message: identifier, tenant, schema type and version, timestamps, tracing and payload |
| DLQ | `DLQ` | `DeadLetter` | Platform | A message that a processing stage could not handle, including the reason |
| Projection | `Проекция` | `Projection` | Reporting / API | A read model built from source events. Rebuild needs retained source history or a compatible snapshot or backup |
| Data quality | `Качество данных` | `DataQuality` | API | A separate `unknown`, `actual` or `stale` indicator; the latest event alone does not prove freshness |
| Device availability | `Доступность устройства` | `DeviceAvailability` | Integration | An observation of equipment availability, independent of connector connection state |
| State observation | `Наблюдение состояния` | `StateObservation` | Integration | Actually observed values, availability, timestamp and source cursor; history is not presented as a new observation |
| Maintenance window | `Окно обслуживания` | `MaintenanceWindow` | Catalog | A one-time maintenance interval for a defined scope, with an explicit processing policy and automatic completion |
| Site duty | `Дежурство площадки` | `SiteDuty` | Response | A versioned assignment of a user responsible for a site, applied to new assignments and notifications |
| Responsibility transfer | `Передача ответственности` | `IncidentTransfer` | Incident | Explicit transfer of an incident requiring acceptance by the receiving user; the previous owner remains responsible until confirmation |
