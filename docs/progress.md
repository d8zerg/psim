# Прогресс разработки MVP

Статусы: не начат · в работе · готов (ждёт утверждения) · утверждён

План: [psim-mvp-development-plan.md](psim-mvp-development-plan.md)

## Принятые дополнения объёма

Владелец продукта принял [ADR-042](adr/0042-operational-capabilities.md): состояние устройств и realtime, файловое хранилище планов, разовое обслуживание, минимальное дежурство и передача ответственности, процедуры сертификатов и изолированный replay. Требования и приёмка обновлены в плане; платформенные механизмы сертификатов реализованы в шаге 2.6, остальные функции относятся к Ф4-Ф9 и пока не выполнены. Завершённые шаги сохраняют прежние статусы; совместимые расширения контрактов вводятся до реализации соответствующих функций.

Опубликованы [правила участия](../CONTRIBUTING.md), [сообщения об уязвимостях](../SECURITY.md) и [политика совместимости и поддержки](engineering/compatibility-and-support.md). Синтетический демонстрационный набор - шаг 11.1.

## Ф0. Архитектура и контракты -> M0

| Шаг | Название | Статус | Артефакты |
|---|---|---|---|
| 0.1 | Видение продукта и границы MVP | утверждён | [product-brief](product/product-brief.md), [roles-and-journeys](product/roles-and-journeys.md), [mvp-scope](product/mvp-scope.md), [acceptance-spec](product/acceptance-spec.md) |
| 0.2 | Доменная модель и контексты | утверждён | [glossary](domain/glossary.md), [context-map](domain/context-map.md), [aggregates](domain/aggregates.md), [state-models](domain/state-models.md), [event-taxonomy](domain/event-taxonomy.md) |
| 0.3 | Архитектурный документ | утверждён | [arc42](architecture/README.md), [runtime](architecture/runtime-view.md), [deployment](architecture/deployment.md), [crosscutting](architecture/crosscutting.md), [data-flows](architecture/data-flows.md), [scaling](architecture/scaling.md), [quality](architecture/quality.md), [C4 DSL](architecture/c4/workspace.dsl) |
| 0.4 | Пакет ADR | утверждён | [реестр ADR](adr/README.md): 17 приняты, 10 предложены (принимаются в начале своих шагов) |
| 0.5 | Контракты v1 | утверждён | [contracts/](../contracts/README.md): proto, OpenAPI, AsyncAPI, реестр топиков, таксономия, каталог ошибок; проверка `task contracts:check`; ADR-028 (ClickHouse), ADR-029 |
| 0.6 | Модель угроз и требования безопасности | утверждён | [threat-model](security/threat-model.md), [реестр угроз и требований](security/threat-register.md) (источник - [threat-model.yaml](security/threat-model.yaml)); проверка `task docs:threats` |
| 0.7 | Стратегия качества и верификации | утверждён | [strategy](quality/strategy.md), [матрица верификации](quality/verification-matrix.md) (источник - [verification.yaml](quality/verification.yaml)), [сверка](quality/reconciliation.md), [замер задержек](quality/latency-measurement.md), ADR-031; проверка `task docs:verification` |

**Веха M0** (документы Ф0 утверждены, контракты v1 опубликованы, ADR приняты): пройдена. Решения со статусом «Предложено» принимаются в начале своих шагов (реестр ADR).

## Ф1. Инженерная платформа

| Шаг | Название | Статус | Артефакты |
|---|---|---|---|
| 1.1 | Монорепозиторий и toolchain | утверждён | [ADR-032](adr/0032-cpp-toolchain.md), `tools/toolchain`, `CMakePresets.json`, `conanfile.py`, `task cpp:*` |
| 1.2 | Контроль качества кода | утверждён | [ADR-033](adr/0033-code-quality-gates.md), [стандарт C++](engineering/cpp-coding-standard.md), [стандарт TypeScript](engineering/typescript-coding-standard.md), `.clang-format`, `.clang-tidy`, `tools/arch`, `tools/git-hooks`; проверки `task cpp:format`, `cpp:lint`, `cpp:test:sanitizers`, `cpp:coverage`, `arch:check`; барьер - `task check` |
| 1.3 | CI/CD и цепочка поставки | утверждён | [ADR-034](adr/0034-local-ci-and-supply-chain.md); конвейеры `task ci` (перед каждым коммитом, [ADR-036](adr/0036-no-git-hooks.md)), `task ci:nightly`, `task release`; пакеты DEB, образы distroless без root, SBOM CycloneDX, Trivy, подпись Ed25519; барьер бенчмарков `bench:compare` |
| 1.4 | Локальное окружение | утверждён | [ADR-035](adr/0035-local-environment.md); `deploy/local/` (Compose, 1 или 3 брокера Kafka), `task env:up`, `env:test`, `env:ci-test` (этап `env` конвейера); realm Keycloak с политикой SR-02; запуск с нуля около 95 с |
| 1.5 | Веб-монорепозиторий | утверждён | [ADR-037](adr/0037-web-monorepo.md); `web/`: консоль (Vite, React 19, TanStack, Zustand, Tailwind 4), дизайн-система, API-клиент из OpenAPI, realtime-клиент из proto; `task web:check` (ESLint, типы, Vitest ≥ 80%, бюджеты size-limit), `task web:e2e` (Playwright); этап `web` конвейера |
| 1.6 | Документация как код | утверждён | [ADR-038](adr/0038-documentation-portal.md); `task docs:portal` (MkDocs + Material, строгий режим): документы, ADR, C4, справочники REST (Redoc), Kafka (AsyncAPI), Protobuf, коды ошибок; диаграммы в SVG, без внешних ресурсов; архив портала в подписанном выпуске; `task docs:serve` |

Фаза Ф1 завершена.

## Ф2. Платформенное ядро

| Шаг | Название | Статус | Артефакты |
|---|---|---|---|
| 2.1 | Runtime сервиса | утверждён | [ADR-039](adr/0039-service-runtime.md); `libs/platform/{errors,log,config,observability,runtime}`, шаблон `tools/service-template`, `task service:new`; FF-06 `task test:observability` |
| 2.2 | Асинхронная модель исполнения | утверждён | [ADR-016](adr/0016-threading-model.md), [ADR-040](adr/0040-async-primitives.md); `libs/platform/async` (шарды, `Clock`/`ManualClock`, `with_deadline`, `Credits`, `Semaphore`, `BoundedQueue`, `BlockingPool`), секция `runtime` конфигурации; B-02 `task bench:async` |
| 2.3 | Слой Kafka | утверждён | [ADR-041](adr/0041-kafka-layer.md); `libs/platform/kafka` (идемпотентный и транзакционный продюсер, потребитель, `Processor` ALO/EOS-K с повторами, DLQ и предохранителем, `ProtobufSerde` и клиент Schema Registry, трассировка в заголовках, метрики); TS-03 `task test:component` (в т. ч. SIGKILL обработчика), FF-04 `task contracts:registry-compat`, B-01 `task bench:kafka` (776 тыс. / 1,26 млн сообщ./с) |
| 2.4 | Слой PostgreSQL | утверждён | [ADR-014](adr/0014-postgresql-async-driver.md); [libs/platform/pg](../libs/platform/pg/README.md) (libpq 18.6 + Asio, pipeline, COPY, уведомления, пул, транзакции, единица работы, expand/contract, inbox/outbox и реле), CLI `psim-pg-migrate`; `task test:component`, `task test:pg:sanitizers`, `task bench:pg` |
| 2.5 | Слой Redis | утверждён | [ADR-015](adr/0015-redis-client-and-role.md); [libs/platform/redis](../libs/platform/redis/README.md) (Boost.Redis 1.90, RESP3, TLS/mTLS, Sentinel, ограниченный пул, байты/Protobuf, кэш, атомарные лимиты, TTL-сессии и Pub/Sub); `task test:redis`, `task test:redis:sanitizers`, `task test:component` |
| 2.6 | Безопасность платформы | утверждён | [ADR-043](adr/0043-platform-security.md); [libs/platform/security](../libs/platform/security/README.md): TLS/mTLS, JWT/JWKS, секреты, CSR, продление и отзыв активных сессий; [регламент доверия](security/platform-trust.md); `task test:security`, `task test:security:sanitizers` |
| 2.7 | Детерминированный симуляционный стенд | не начат | |
| 2.8 | Слой ClickHouse | не начат | |
| 2.9 | Слой S3-совместимого объектного хранилища | не начат | |

### Проверка шага 2.4

- 4 модульных и 17 компонентных сценариев PostgreSQL проходят. Проверены отмена, дедлайны, read-only endpoint, сетевой разрыв и восстановление; SIGKILL до COMMIT, после COMMIT и после подтверждения Kafka сохраняет зафиксированные события и исключает повторные эффекты inbox.
- 101 тест проходит под ASan/UBSan и TSan; компонентные сценарии PostgreSQL проходят под обоими санитайзерами. Все 26 компонентных сценариев Kafka и PostgreSQL проходят под TSan после атомарного патча librdkafka из ADR-041. Проверка PostgreSQL под санитайзерами включена в `env:ci-test`; изменения модуля запускают этап окружения CI.
- Покрытие PostgreSQL - 79,01% при пороге 75%. clang-tidy, форматирование, архитектурные ограничения и проверка зависимостей/лицензий проходят. CLI проверен повторным expand/contract и поставляется отдельным DEB-пакетом; пакеты устанавливаются, запускаются и удаляются на Debian 13 x86_64.
- `task bench:pg`: COPY - около 1,08 млн строк/с, pipeline - 55,5 тыс. строк/с, запрос p50/p99 - 0,075/0,131 мс на локальном окружении. Проверка B-01 Kafka после патча также проходит: 791 тыс. / 1,08 млн сообщ./с при пороге 200 тыс.

Шаг 2.4 утверждён владельцем. Проверка полноценного HA-кластера с Patroni и переключением primary относится к шагу 9.1, как указано в ADR-014.

### Проверка шага 2.5

- 5 модульных и 15 компонентных сценариев Valkey проходят. Проверены бинарные параметры и pipeline, исключение чувствительных данных из ошибок, ограничения запросов/ответов, отмена и дедлайны без автоматического повторения записи, ограниченный пул, TTL, Protobuf, изоляция тенантов, атомарные лимиты и токены поколения сессий. Уведомление проверено настоящим RESP3-подписчиком.
- Стенд primary + 2 реплики + 3 Sentinel начинает каждый прогон с чистой топологии. После остановки ответов primary Sentinel автоматически повышает реплику, а пул обнаруживает её и возобновляет записи под нагрузкой. Отдельно проходит восстановление после разрыва через Toxiproxy.
- TLS/mTLS проверены с доверенным CA и сертификатом клиента, а также с неверным именем сервера, недоверенным CA и отсутствующим клиентским сертификатом. Все 106 тестов без внешней инфраструктуры проходят под ASan/UBSan и TSan; 15 компонентных сценариев Valkey проходят под обоими санитайзерами.
- Общая проверка всех 41 компонентного сценария Kafka, PostgreSQL и Valkey проходит. Покрытие нового модуля - 86,27% при пороге 75%. clang-tidy, форматирование, архитектурные ограничения, зависимости/лицензии, документация и запуск на Debian 13 x86_64 проверены. Компонентные проверки Valkey под санитайзерами подключены к `env:ci-test`; изменение модуля включает этап окружения CI.

Шаг 2.5 утверждён владельцем. Топология MVP следует ADR-015: standalone или primary с репликами под Sentinel. Шардирование Cluster не входит в MVP; механизмы ротации сертификатов реализованы в шаге 2.6, эксплуатационное размещение узлов, ACL и автоматизация относятся к шагу 9.1.

### Проверка шага 2.6

- 22 модульных и 6 компонентных сценариев безопасности проходят. Проверены защищённые файлы и перемещение `Secret`, TLS/mTLS с проверкой CA и имени, чужой CA, отсутствие и истечение сертификата, свежий/отозвавший/устаревший CRL, CSR P-256, публикация TLS-контекста с сохранением последнего корректного состояния, HTTPS с ограничениями размера и дедлайном.
- JWT/JWKS проверены для RS256 и ES256, фиксированных issuer/audience, срока и допуска часов, ролей выбранного клиента, ротации и удаления ключей, ограниченного кэша при отказе IdP, подмены алгоритма/адреса ключа, повторяющихся полей и некорректных входов. Настоящий токен Keycloak проверяется по его JWKS через HTTPS и связывается с mTLS-идентичностью сервиса.
- Продление сохраняет старый mTLS-поток; через два потока проходят 200 последовательных тестовых событий. Отзыв закрывает прежнюю сессию и блокирует повторное использование её сертификата, восстановление закрывает оставшиеся прежние сессии. Истечение закрывает поток даже после очистки записи старого сертификата при следующем продлении.
- Настоящие Kafka, PostgreSQL и Valkey проходят проверки защищённого транспорта. Все 47 компонентных сценариев платформы успешны; покрытие адаптеров: Kafka - 79,65%, PostgreSQL - 79,15%, Redis - 85,91%, порог - 75%.
- Все 128 тестов без внешней инфраструктуры проходят под ASan/UBSan и TSan. Все 28 проверок безопасности, включая 6 компонентных, проходят под обоими санитайзерами; SEC-08 подключён к этапу окружения CI. Модульное покрытие библиотеки безопасности - 90,91% при пороге 75%.
- clang-tidy, форматирование, архитектурные ограничения, зависимости/лицензии и документация проверены. Библиотека запускается на Debian 13 x86_64; проверка FF-06 подтверждает health, метрики, логи, трассировку и завершение сервиса.

Шаг 2.6 утверждён владельцем. Выбрана внешняя step-ca/step CLI либо PKI организации; собственный УЦ платформы не реализуется. Библиотечные механизмы готовы; постоянный каталог, авторизация оператора, аудит и распространение отзыва между шлюзами относятся к шагу 4.2, бизнес-права - к Ф7, автоматизация TLS/ACL поставки - к шагу 9.1, ClickHouse - к шагу 2.8. Шаг 2.7 не начат.

## Ф3-Ф11

Не начаты.

## Открытые решения владельца

См. раздел 11 [product brief](product/product-brief.md). Q1 закрыт: один инженер и ИИ-агент, сокращения объёма приняты. Принято решение использовать ClickHouse для истории событий и аналитики (ADR-028). Q2 закрыт: симулятор и Webhook/HTTP-коннектор. Q3 закрыт: партнёров нет, партнёрский пилот не является условием выпуска MVP; возвращаемся к нему при появлении участника. Q4 закрыт: публичное название - Open-Source PSIM Platform, репозиторий и пространства имён - `psim`, стенд, образы и поставка - `psim-platform`. Открытых вопросов Q1-Q4 нет.
