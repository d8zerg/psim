# Сквозной стенд M1

Шаг 3.1, [ADR-047](../adr/0047-event-flow.md).

```text
simulator -> Connector Gateway -> Kafka raw -> Normalizer -> Kafka normalized
          -> Correlation Engine -> Kafka signals -> Incident Service + PostgreSQL outbox
          -> Kafka incident events -> Projection Service -> PostgreSQL / Redis -> API Gateway -> браузер
          -> Kafka incident events -> API Gateway -> WebSocket -> браузер
```

## Запуск

```bash
task flow:up
task flow:emit COUNT=3
task flow:down
```

Откройте `http://localhost:4173`, нажмите Sign in и войдите как `operator1` с паролем `psim-dev-Passw0rd`. Лента получает исходный снимок и обновляется через WebSocket. `flow:down` останавливает прикладные процессы; инфраструктура и данные остаются. Для полной остановки - `task env:down`. Удаление данных - только явный `task env:reset`.

`compose.event-flow.yaml` подключён к базовому Compose как необязательный профиль. Сервисы используют собранные C++-бинарные файлы и консоль из workspace. `deploy/local/config/event-flow` задаёт один shard, тестовые endpoints и явное разрешение plaintext. PKI и защищённый файл пароля БД находятся в игнорируемом `.cache/event-flow`; это только учётные данные разработки. Keycloak fixture согласуется также с существующей БД realm.

## Сквозная проверка

```bash
task env:up
task flow:test
# Полный путь под санитайзерами:
task flow:sanitizers
```

В CI задача использует уже запущенную инфраструктуру проекта `psim-ci`, поднимает и затем останавливает свои прикладные процессы. `test:component` добавляет этот поток к профилям покрытия; `flow:sanitizers` повторяет его под ASan/UBSan и TSan. Браузер делит сетевое пространство console-контейнера и открывает loopback-адрес: PKCE использует Web Crypto в разрешённом браузером локальном контексте. Небезопасные флаги Chromium не требуются.

Проверяются вход OIDC, отказ анонимному и неверному токену, отказ технической роли в чтении ленты, mTLS-ввод, нормализация, одно правило, inbox/outbox и обновление DOM. После трёх прогревочных событий идут 30 событий с интервалом 200 мс, каждое отправляется дважды. Ожидаются 60 raw, 30 normalized, 30 signal records и 30 доменных изменений для измеряемых событий. Разрыв браузерного соединения проверяет resume; SIGKILL API - новую эпоху и новый согласованный снимок. После перезапуска отправляется ещё одно событие. RESTORE не создаёт сигнал.

Артефакты - `build/event-flow-test`: журнал Playwright, исходные события, измерения отрисовки, `baseline.json`, причинная `trace.json` и снимок ленты `incident-feed.png`. Проверка сохраняет отдельные наблюдения; p50/p99 считаются по исходным значениям методом ближайшего ранга. Все контейнеры одного хоста используют общие часы ядра; для распределённого стенда нужна методика [синхронизации](../quality/latency-measurement.md).

Базовая линия debug: 30 измерений, 5 событий/с, максимальное опоздание генератора 0,91 мс; p50/p99 до сигнала около 35/50 мс, до браузера около 66/80 мс. Это замер функционального стенда на одном хосте, не подтверждение SLO под нагрузкой. Для сравнения последующих изменений используйте профиль, параметры и сырые данные из конкретного `baseline.json`.

## Ответственность процессов

| Процесс | Минимальная реализация |
|---|---|
| simulator | `tools/event-flow/psim-flow-simulator`: настоящий gRPC-клиент, PKI fixture, открытое расписание и повтор идентичных событий |
| connector-gateway | URI SAN известного коннектора, Register, событие, BatchAck после Kafka, Credits |
| normalizer | Один источник в fixture-каталоге, ALARM/RESTORE, типизированный EventRecord |
| correlation-engine | Однособытийное правило тревоги, детерминированный signal_id |
| incident-service | Группировка на 60 секунд, версия, inbox, агрегат и outbox в одной транзакции |
| projection-service | Общая проекция PostgreSQL, кэш Redis, scoped GET `/v1/feed/incidents` |
| api-gateway | JWT, HTTPS к Projection Service, WebSocket `/v1/realtime` |
| console | Вход PKCE, снимок, версии агрегатов и дельты без периодического REST-опроса |

Текущее владение снимками уточнено в [ADR-059](../adr/0059-console-projections.md). Полные функции следующих шагов перечислены в ADR-047. Не используйте fixture-каталог, общий локальный пользователь БД или PKI стенда для поставки.
