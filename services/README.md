# Сервисы

Один каталог на сервис платформы (ADR-001): `application`, `adapters`, `main`. Сервис зависит от своего доменного ядра из `libs/domain`, от `libs/platform` и от сгенерированных контрактов (`psim::contracts`), но не от других сервисов.

Сервис создаётся из шаблона ([ADR-039](../docs/adr/0039-service-runtime.md)):

```
task service:new NAME=incident-service CONTEXTS=incident SUMMARY="PSIM Incident Service"
```

Генератор копирует `tools/service-template` в `services/<имя>` (исполняемый файл `psim-<имя>`, пакет DEB, образ `deploy/images/psim-<имя>`) и регистрирует сервис в `tools/arch/architecture.yaml` с его контекстами домена. Сервис сразу получает конфигурацию (YAML и `PSIM__*`), JSON-логи, метрики `/metrics`, трассировку OTLP, `/health/live`, `/health/ready` и остановку с дренированием; проверка - `task test:observability` (FF-06). Минимальные Connector Gateway, Normalizer, Correlation Engine, Incident Service и API Gateway работают в [стенде M1](../docs/engineering/event-flow.md).

Полная нормализация и загрузка истории описаны в [руководстве](../docs/engineering/normalization-and-history.md). `task processing:test` проверяет дедупликацию и восстановление Kafka -> ClickHouse.

[Command Service](../docs/engineering/command-service.md) управляет командами устройствам, подтверждениями, дедлайнами и результатами. `task commands:test` проверяет доставку через настоящий шлюз и SDK на двух экземплярах сервиса.

[Response Engine](../docs/engineering/response-engine.md) исполняет версии сценариев, сохраняет переходы и таймеры, передаёт команды Command Service. `task response:test` проверяет восстановление двух экземпляров и replay без внешних эффектов.
