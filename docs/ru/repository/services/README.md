# Сервисы

<!-- language-links -->
[English](../../../../services/README.md) | [Русский](README.md)
<!-- /language-links -->

Один каталог на сервис платформы (ADR-001): `application`, `adapters`, `main`. Сервис зависит от своего доменного ядра из `libs/domain`, от `libs/platform` и от сгенерированных контрактов (`psim::contracts`), но не от других сервисов.

Сервис создаётся из шаблона ([ADR-039](../../adr/0039-service-runtime.md)):

```
task service:new NAME=example-service CONTEXTS=incident SUMMARY="Example Incident Service"
```

Генератор копирует `tools/service-template` в `services/<name>` (исполняемый файл `psim-<name>`, пакет DEB, образ `deploy/images/psim-<name>`) и регистрирует сервис в `tools/arch/architecture.yaml` с его контекстами домена. Сервис сразу получает конфигурацию (YAML и `PSIM__*`), JSON-логи, метрики `/metrics`, трассировку OTLP, `/health/live`, `/health/ready` и остановку с дренированием; проверка - `task test:observability` (FF-06). Connector Gateway, Normalizer, Correlation Engine, Incident Service и API Gateway работают в [сквозном стенде](../../engineering/event-flow.md).

Полная нормализация и загрузка истории описаны в [руководстве](../../engineering/normalization-and-history.md). `task processing:test` проверяет дедупликацию и восстановление Kafka -> ClickHouse.

[Command Service](../../engineering/command-service.md) управляет командами устройствам, подтверждениями, дедлайнами и результатами. `task commands:test` проверяет доставку через настоящий шлюз и SDK на двух экземплярах сервиса.

[Response Engine](../../engineering/response-engine.md) исполняет версии сценариев, сохраняет переходы и таймеры, передаёт команды Command Service. `task response:test` проверяет восстановление двух экземпляров и replay без внешних эффектов.

[Notification Service](../../engineering/notifications.md) доставляет уведомления в приложении, через SMTP с TLS и подписанный webhook. Response Engine управляет SLA и дежурствами. `task notifications:test` проверяет квитанции, повторы, смену дежурного и восстановление процессов.

[Audit Service](../../engineering/audit-service.md) строит цепочки хэшей, подписывает внешние контрольные точки и предоставляет поиск и экспорт. `psim-audit-tool` выполняет миграции и независимую проверку. `task audit:test` проверяет историю Kafka, две копии сервиса и вмешательство администратора БД.

[Projection Service](../../engineering/incident-reports.md) загружает факты инцидентов и исходные значения SLA, предоставляет отчёты и CSV. `psim-report-tool` пересчитывает факты из PostgreSQL. Проверки: `task reports:test`, `task reports:bench`.
