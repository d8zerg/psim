# Развёртывание

| Каталог | Содержимое | Шаг |
|---|---|---|
| `local/` | Локальное окружение разработки и интеграционного тестирования (Docker Compose) | 1.4, [ADR-035](../docs/adr/0035-local-environment.md) |
| `images/` | Контейнерные образы сервисов (distroless, без root) | 1.3, [ADR-034](../docs/adr/0034-local-ci-and-supply-chain.md) |
| `keys/` | Открытый релизный ключ подписи (появится к первому выпуску) | 1.3, 11.3 |

Systemd-юниты, установщик и пакет наблюдаемости - фаза Ф9.

## Локальное окружение

```
task env:up              # запустить (KAFKA=3 - три брокера; смена числа брокеров - через env:reset)
task env:endpoints       # адреса и учётные данные разработки
task env:test            # смоук-тесты запущенного окружения
task env:ps              # состояние контейнеров; env:logs SERVICE=<имя> - журналы
task env:down            # остановить с сохранением данных; env:reset - удалить данные
task env:ci-test         # окружение конвейера: с нуля, без портов, бюджет 5 мин, смоук-тесты, удаление
```

- `local/compose.yaml` - все компоненты; `local/compose.ports.yaml` - порты интерактивного окружения, только на `127.0.0.1`.
- `local/config/` - конфигурации: PostgreSQL (базы и роли), ClickHouse и Keeper, realm Keycloak `psim`, OpenTelemetry Collector, Prometheus, Loki, Tempo, источники Grafana, прокси Toxiproxy.
- Сервисы и тесты в сети Compose обращаются к компонентам по именам (`kafka-1:19092`, `postgres:5432`, `clickhouse:9000`, `valkey:6379`, `registry:8080`, `keycloak:8080`, `otel-collector:4317`); для сетевых сбоев - через `toxiproxy` (Kafka `toxiproxy:29092`, PostgreSQL `:15432`, Valkey `:16379`, ClickHouse `:19000`).
- Топики Kafka создаются из реестра [`contracts/topics/topics.yaml`](../contracts/topics/topics.yaml); автосоздание выключено.
- Учётные данные `psim-dev-*` - только для разработки.

## Стенд Valkey HA

`task env:valkey:ha` запускает профиль `valkey-ha`: отдельный primary, две реплики, три Sentinel с quorum 2 и mTLS-endpoint `valkey-tls:6380`. Каждый запуск пересоздаёт только этот тестовый профиль с чистой топологией; сертификаты генерируются в игнорируемом `.cache/valkey-tls`. Его автоматически используют `task test:redis`, `task test:redis:sanitizers` и `task test:component`. Профиль не публикует порты и изолирован от обычного Valkey. Sentinel использует пароль `psim-dev-sentinel`; хранилище - `psim-dev-valkey`. `env:down` останавливает также HA-контейнеры, `env:reset` удаляет их состояние.

Тесты используют `PSIM_TEST_REDIS`, `PSIM_TEST_REDIS_FAULT` и список `PSIM_TEST_SENTINELS` из `cpp:_run_env`. В производственном кластере размещение и TLS определяются [ADR-015](../docs/adr/0015-redis-client-and-role.md) и фазой Ф9.

## Стенд безопасности

`task env:security` запускает профиль `security`: изолированные `kafka-tls:19092`, `postgres-tls:5432` и `valkey-tls:6380`, с проверкой клиентского сертификата. Он пересоздаёт только тестовые узлы, не публикует порты, генерирует сертификаты в `.cache/security-tls` и `.cache/valkey-tls`. `task test:security`, `task test:security:sanitizers` и `task test:component` используют этот профиль; Keycloak получает `psim` в audience access token консоли и сервиса. `PSIM_TEST_KEYCLOAK` указывает на изолированный backend разработки, а библиотечный клиент JWKS проверяется через HTTPS.

Обычный Compose сохраняет протоколы и учётные данные разработки. Клиенты выбирают исключения явно; поставка использует TLS и отдельные сервисные права по [ADR-043](../docs/adr/0043-platform-security.md). Тестовая PKI не используется при установке платформы; внешняя PKI оператора описана в [регламенте доверия](../docs/security/platform-trust.md).

S3-совместимое файловое хранилище MVP - SeaweedFS; клиент и требования интеграции описаны в [слое S3](../libs/platform/s3/README.md). Локальный bucket закрыт, роли установки и приложения разделены. Конфигурация HA и автоматизация поставки относятся к шагу 9.1, резервирование - к 9.3.
