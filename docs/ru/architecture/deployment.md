# Представление развёртывания

<!-- language-links -->
[English](../../architecture/deployment.md) | [Русский](deployment.md)
<!-- /language-links -->

Раздел 7 arc42. Топологии MVP: одна машина и кластер на VM. Kubernetes вне MVP (X15), но образы готовы к нему: без root, конфигурация через файлы и переменные, health-эндпоинты, корректная остановка по `SIGTERM`. Решения - ADR-023 и ADR-024; реализация описана в [топологиях](../engineering/deployment-topologies.md) и [регламенте обновления](../engineering/installation-and-upgrades.md).

---

## 1. Артефакты поставки

| Артефакт | Содержимое |
|---|---|
| Контейнерные образы | Один образ на сервис; минимальная база; пользователь без root; подпись и SBOM |
| Пакеты `.deb` | Бинарники приложений и общий пакет таксономии; systemd-юниты и конфигурацию формирует профиль. Инфраструктура использует Docker Compose на Debian 13 x86_64 |
| Веб-приложение | Статические файлы, раздаются API Gateway или веб-сервером |
| Установщик | Проверка предусловий, генерация конфигурации по профилю топологии, создание топиков и схем БД, загрузка realm Keycloak |
| Пакет наблюдаемости | Конфигурация Collector, Prometheus, дашборды Grafana, правила алертов, runbook |
| CLI администратора | `psimctl` |
| Файловое хранилище | SeaweedFS с закрытым bucket `psim-plans`; версии планов ведёт Resource Catalog, независимая резервная копия - вне рабочего кластера ([ADR-042](../adr/0042-operational-capabilities.md), [ADR-046](../adr/0046-s3-object-storage.md)) |

Сервисы одинаковы во всех топологиях; различаются конфигурация, число экземпляров и параметры инфраструктуры. Первоначальная настройка включает внешнюю PKI, доверие mTLS и Keycloak по [эксплуатационному регламенту](operational-capabilities.md#6-сертификаты-секреты-и-восстановление-доступа).

Реализованные профили и команды установки: [поставка двух топологий](../engineering/deployment-topologies.md), [локальные VM](../engineering/local-vm-lab.md), [первоначальная PKI и учётные записи](../engineering/deployment-pki.md), [обновление и откат](../engineering/installation-and-upgrades.md). Локальный кластер размещает три гостевые ОС на одном физическом хосте; независимые физические домены отказа проверяются отдельным размещением.

## 2. Топология «одна машина»

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

top to bottom direction
package "Сервер: Debian 13 x86_64" as HOST {
package "Инфраструктура" as INFRA {
rectangle "Kafka, 1 брокер KRaft" as K
rectangle "Schema Registry" as SR
database "PostgreSQL" as PG
database "Redis-совместимое хранилище" as R
database "ClickHouse + Keeper" as CH
rectangle "Keycloak" as KC
database "SeaweedFS: планы" as FS
}
package "Сервисы PSIM: по 1 экземпляру" as APP {
rectangle "Connector Gateway" as S1
rectangle "Normalizer, Event History" as S2
rectangle "Correlation Engine" as S3
rectangle "Incident, Response, Command, Notification" as S4
rectangle "Projection, Audit, Catalog" as S5
rectangle "API Gateway + веб" as S6
}
package "Наблюдаемость" as OBS {
rectangle "OTel Collector" as OC
rectangle "Prometheus" as PR
rectangle "Grafana" as GR
}
}
rectangle "Коннекторы" as CONN
rectangle "Браузеры" as USR
OC --> PR
PR --> GR
CONN --> S1 : :7443 gRPC mTLS
USR --> S6 : :443 HTTPS/WSS
@enduml
```

Запуск - Docker Compose или systemd-юниты из пакетов `.deb`.

**Границы надёжности** (документируются для заказчика явно):

| Свойство | Значение |
|---|---|
| Отказ процесса сервиса | Перезапуск supervisor'ом (Docker или systemd); без потерь, RTO - время перезапуска |
| Отказ сервера | Простой до восстановления сервера |
| Отказ диска | Потеря данных после последней резервной копии; рекомендуется RAID 1 или 10 и архивирование WAL PostgreSQL |
| RPO | 0 при отказе процесса; интервал резервного копирования при потере диска |
| Обновление | С кратким простоем обработки: события буферизуются в SDK коннекторов и досылаются |
| Производительность | Цель 10 000 событий/с на эталонном сервере ([scaling.md](scaling.md#54-эталонные-стенды)) |

## 3. Топология «кластер на VM»

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

top to bottom direction
rectangle "HAProxy ×2 + keepalived VIP\n:443 HTTPS/WSS, :7443 gRPC L4" as LB
package "infra-1" as I1 {
rectangle "Kafka broker+controller" as K1
database "PostgreSQL + Patroni" as P1
rectangle "etcd" as E1
database "Redis master/replica" as R1
database "ClickHouse реплика + Keeper" as C1
database "SeaweedFS master, volume, filer, S3" as F1
}
package "infra-2" as I2 {
rectangle "Kafka broker+controller" as K2
database "PostgreSQL + Patroni" as P2
rectangle "etcd" as E2
database "Redis master/replica" as R2
database "ClickHouse реплика + Keeper" as C2
database "SeaweedFS master, volume, filer, S3" as F2
}
package "infra-3" as I3 {
rectangle "Kafka broker+controller" as K3
database "PostgreSQL + Patroni" as P3
rectangle "etcd" as E3
database "Redis master/replica" as R3
rectangle "ClickHouse Keeper" as C3
rectangle "SeaweedFS master" as F3
}
package "app-1" as A1 {
rectangle "Экземпляры сервисов PSIM, Keycloak" as SA
}
package "app-2" as A2 {
rectangle "Экземпляры сервисов PSIM, Keycloak" as SB
}
rectangle "obs-1: Collector, Prometheus, Grafana,\nлоги и трассировки" as OBS
LB --> A1
LB --> A2
A1 --> I1
A1 --> I2
A1 --> I3
A2 --> I1
A2 --> I2
A2 --> I3
A1 ..> OBS
A2 ..> OBS
@enduml
```

| Компонент | Конфигурация | Поведение при отказе одного узла |
|---|---|---|
| Kafka | 3 узла в совмещённом режиме broker + controller (KRaft); RF 3, `min.insync.replicas` 2 | Новые лидеры партиций за секунды; запись продолжается |
| PostgreSQL | Patroni + etcd (3 узла); одна синхронная реплика (`synchronous_mode: true`), одна асинхронная; доступ через HAProxy по роли лидера | Переключение ≤ 30 с; зафиксированные транзакции не теряются |
| Redis-совместимое хранилище | Valkey: primary + 2 реплики на разных узлах, 3 Sentinel с quorum 2 ([ADR-015](../adr/0015-redis-client-and-role.md)). Обнаружение primary и проверка ROLE выполняются клиентом; шардирование Cluster в MVP не используется | Реплика становится мастером; кэши и лимиты допускают кратковременную потерю, реестр сессий восстанавливается переподключением коннекторов |
| Сервисы PSIM | ≥ 2 экземпляра каждого, на разных узлах приложений ([scaling.md](scaling.md#53-экземпляры-сервисов-при-50-000-событийс-кластер)) | Партиции переходят к оставшимся экземплярам |
| Keycloak | 2 экземпляра, база в PostgreSQL кластера | Уже выданные токены проверяются по кэшу JWKS; новый вход - через оставшийся экземпляр |
| HAProxy | 2 экземпляра с общим VIP (keepalived) | Переключение VIP за секунды |
| ClickHouse | 1 шард × 2 реплики (ReplicatedMergeTree) на infra-1 и infra-2; ClickHouse Keeper на трёх узлах инфраструктуры ([ADR-028](../adr/0028-clickhouse-analytical-storage.md)) | Чтение и запись переходят на вторую реплику; после восстановления реплика догоняет; загрузчики Kafka догружают накопившееся |
| Файловое хранилище | SeaweedFS: 3 master (Raft), volume-серверы с репликацией томов на разных узлах, 2 filer и 2 S3 gateway за HAProxy; хранилище метаданных filer и шифрование томов фиксируются ADR-077 | Текущие планы читаются при отказе одного узла; незавершённая замена сохраняет прежнюю версию |
| Наблюдаемость | Отдельный узел; не влияет на обработку | Потеря телеметрии на время отказа; сервисы не блокируются экспортом |

Минимальная конфигурация - 3 узла, на которых совмещены инфраструктура и сервисы; эталонная для 50 000 событий/с - 3 узла инфраструктуры + 2 узла приложений + узел наблюдаемости.

## 4. Обновление

Обновление без простоя (S12, QS-O1) в кластере:

1. **Предпроверка.** Установщик сверяет совместимость: схемы Protobuf N+1 совместимы с N, миграции N+1 только расширяющие, версия протокола коннекторов поддерживается.
2. **Расширяющие миграции.** Применяются до замены кода; код N продолжает работать на расширенной схеме.
3. **Регистрация схем.** Новые версии схем регистрируются в Schema Registry.
4. **Последовательная замена.** По одному экземпляру: `ready = false` -> дренирование -> остановка -> запуск N+1 с той же идентичностью экземпляра сервиса -> ожидание `ready` и отсутствия роста лага -> следующий экземпляр. Порядок сервисов - от потребителей к производителям, чтобы новые потребители были готовы к новым полям до их появления.
5. **Сужающие миграции.** Выполняются в следующем выпуске (N+2), когда код N гарантированно выведен.
6. **Откат.** Последовательная замена обратно на N; возможна, пока не выполнены сужающие миграции N+1.

Для контейнеров Normalizer и Correlation Engine реализован запуск дополнительной реплики с отдельной producer identity до остановки старой; назначения Kafka и готовность проверяются перед передачей обработки. Подробности и фактические границы проверки приведены в [руководстве по обновлению](../engineering/installation-and-upgrades.md).

Квалификация S12/C-12 не завершена и остаётся обязательной перед выпуском. Обновление без нарушения SLO под LOAD-50K пока не подтверждено. На одной машине последовательная замена допускает краткий простой каждого сервиса.

## 5. Сетевые порты

| Порт | Компонент | Доступ |
|---|---|---|
| 443 | API Gateway: HTTPS, WSS, веб-приложение | Пользователи |
| 7443 | Connector Gateway: gRPC, mTLS | Коннекторы |
| 8443 | Keycloak | Пользователи (вход), сервисы |
| 9092-9094 | Kafka (TLS) | Только сервисы платформы |
| 9440, 8443 | ClickHouse: нативный протокол TLS, HTTPS | Только сервисы платформы |
| 9234, 9281 | ClickHouse Keeper: межузловой протокол, клиентский протокол TLS | Только узлы ClickHouse |
| 5432 | PostgreSQL (через HAProxy: 5000 - лидер, 5001 - реплики) | Только сервисы платформы |
| 6379 | Redis-совместимое хранилище (TLS) | Только сервисы платформы |
| 18333 (S3 HTTPS) | SeaweedFS S3 gateway | Только сервисы платформы |
| 9333, 8080, 8888 и gRPC-порты +10000 | SeaweedFS master, volume, filer | Только узлы SeaweedFS |
| 8081 | Schema Registry | Только сервисы платформы |
| 4317 | OTel Collector (OTLP gRPC) | Только сервисы платформы |
| 9090, 3000 | Prometheus, Grafana | Эксплуатация |
| 8080 на каждом сервисе | `/metrics`, `/health/*` | Prometheus, балансировщик |

Внутренние порты закрыты для сетей пользователей и коннекторов межсетевым экраном; установщик выдаёт шаблон правил.
