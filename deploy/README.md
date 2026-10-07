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
