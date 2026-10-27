# Bounded contexts и карта контекстов

<!-- language-links -->
[English](../../domain/context-map.md) | [Русский](context-map.md)
<!-- /language-links -->

## 1. Контексты

| Контекст | Ответственность | Сервисы | Владеет данными |
|---|---|---|---|
| **Integration** | Подключение коннекторов, приём сырых событий, доставка команд | Connector SDK, Connector Gateway | Постоянные регистрации и состояние восстановления (PostgreSQL), временные сессии (Valkey) |
| **Catalog** | Модель защищаемых объектов, устройств, источников, коннекторов | Resource Catalog | Локации, устройства, источники, типы, коннекторы, планы |
| **Event Processing** | Нормализация, обогащение, хранение нормализованных событий, корреляция, правила | Normalizer, Event History, Correlation Engine, управление правилами | Правила отображения, правила корреляции, наборы правил, состояние корреляции, нормализованные события |
| **Incident Management** | Жизненный цикл инцидентов, группировка сигналов, назначение, хронология | Incident Service | Инциденты, типы инцидентов |
| **Response** | Сценарии реагирования, команды на устройства, SLA, эскалации, уведомления | Response Engine, Command Service, Notification Service | Сценарии, запуски, команды, политики эскалации, уведомления, таймеры |
| **Identity & Access** | Пользователи, роли, области доступа, решения о доступе | Keycloak, библиотека политик, API Gateway | Назначения ролей и областей доступа |
| **Audit** | Неизменяемый журнал действий | Audit Service | Записи аудита, контрольные точки |
| **Reporting** | Read-модели консоли и отчёты | Projection Service, Report API | Read-модели консоли и факты для отчётов |
| **Platform** | Общие механизмы: конверт, конфигурация, наблюдаемость, DLQ | Runtime, библиотеки, CLI | Конфигурация системы, retention |

## 2. Карта контекстов

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

left to right direction
package "Upstream" as Upstream {
rectangle "Catalog" as CAT
rectangle "Identity & Access" as IAM
}
rectangle "Integration" as INT
rectangle "Event Processing" as EVP
rectangle "Incident Management" as INC
rectangle "Response" as RSP
rectangle "Audit" as AUD
rectangle "Reporting" as REP
database "Keycloak" as KC
database "Внешние системы" as EXT
EXT --> INT : ACL: Connector SDK
INT --> EVP : PL: RawEvent
CAT --> EVP : OHS/PL: psim.catalog.v1
CAT --> INT : OHS/PL: psim.catalog.v1
CAT --> RSP : OHS/PL: psim.catalog.v1
EVP --> INC : PL: Signal
INC --> RSP : PL: IncidentEvents
RSP --> INC : Customer/Supplier: команды по инциденту
RSP --> INT : PL: Command
INT --> RSP : PL: CommandResult
KC --> IAM : ACL: OIDC/JWT
IAM --> REP : Shared Kernel: политики доступа
INC --> REP
RSP --> REP
CAT --> REP
INT ..> AUD : AuditEvent
CAT ..> AUD : AuditEvent
EVP ..> AUD : AuditEvent
INC ..> AUD : AuditEvent
RSP ..> AUD : AuditEvent
IAM ..> AUD : AuditEvent
@enduml
```

Обозначения: **PL** - Published Language (версионируемая схема Protobuf в топике); **OHS** - Open Host Service; **ACL** - Anti-Corruption Layer; **Customer/Supplier** - нижестоящий контекст влияет на контракт вышестоящего; **Shared Kernel** - общая библиотека.

## 3. Отношения

| Вышестоящий -> нижестоящий | Тип | Канал | Примечание |
|---|---|---|---|
| Внешние системы -> Integration | ACL | Connector SDK, gRPC | Модель внешнего оборудования переводится в `RawEvent` в коннекторе, ядро не знает о производителях |
| Integration -> Event Processing | PL | `psim.ingest.raw.v1` | Integration не интерпретирует содержимое события, только проверяет схему и права |
| Catalog -> Event Processing, Integration, Response | OHS + PL | `psim.catalog.v1` (compacted) | Потребители держат локальную копию каталога и не обращаются к Catalog синхронно на горячем пути |
| Event Processing -> Incident Management | PL | `psim.signals.v1` | Incident не знает правил; сигнал самодостаточен: тип инцидента, приоритет, ключ группировки |
| Incident Management -> Response | PL | `psim.incidents.events.v1` | Response запускает сценарии и таймеры SLA по событиям инцидента |
| Response -> Incident Management | Customer/Supplier | `psim.response.events.v1` | Incident учитывает незавершённые обязательные шаги при решении и отражает шаги в хронологии |
| Response -> Integration | PL | `psim.commands.v1`, `psim.commands.results.v1` | Command Service владеет жизненным циклом команды, шлюз только доставляет её в сессию |
| Keycloak -> Identity & Access | ACL | OIDC, JWKS | Роли и области доступа приводятся к внутренней модели `Principal` |
| Все -> Audit | Conformist (Audit подстраивается) | `psim.audit.v1` | Каждый контекст публикует записи аудита в общем формате; Audit их не интерпретирует |
| Все -> Reporting | Conformist | топики домена | Для rebuild нужны сохранённая история источников и, при необходимости, совместимый snapshot или backup |
| Platform -> все | Shared Kernel | библиотеки | Конверт, runtime, ошибки, наблюдаемость, абстракция времени |

## 4. Правила границ

1. Контекст изменяет только свои данные. Чужие данные доступны ему как локальная копия из топика или через запрос к API владельца вне горячего пути.
2. Межконтекстное взаимодействие на горячем пути - только через Kafka. Синхронные вызовы допускаются для операций пользователя (REST через API Gateway) и для чтения справочных данных вне горячего пути.
3. Доменное ядро каждого контекста - библиотека без зависимостей от Kafka, PostgreSQL, Redis, сети и системных часов. Проверяется архитектурными проверками зависимостей.
4. Идентификаторы чужих агрегатов хранятся как значения; ссылки на чужие объекты не разыменовываются внутри транзакции.
5. Контракты между контекстами (схемы Protobuf) - публичные версионируемые артефакты в каталоге `contracts/`.

## 5. Точки расширения для ИИ (ADR-025)

Точки расширения для анализа событий, обработчиков реагирования и помощи оператору:

- **Потребитель топиков** - новый контекст читает `psim.events.normalized.v1` или `psim.signals.v1` и публикует свои сигналы в `psim.signals.v1` как ещё одно «правило».
- **Шаг сценария** - тип шага `automated` вызывает внешний обработчик через webhook и получает рекомендацию.
- **Ассистент оператора** - потребитель проекций и API с ролью только для чтения.

## 6. Эксплуатационные дополнения (ADR-042)

Catalog владеет интервалами обслуживания и метаданными планов; Integration определяет доступность и сверку источника, Projection строит состояние с качеством данных; Incident отвечает за передачу ответственности; Response Engine владеет назначениями дежурных. Библиотеки платформы предоставляют FileStore и механизмы доверия. Диагностический replay запускается инструментами эксплуатации в отдельном контуре. [Модели и инварианты](../architecture/operational-capabilities.md).
