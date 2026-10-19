# Коннекторы

- [SDK и руководство интегратора](../docs/engineering/connector-sdk.md): публичная цель `psim::platform_connector`, пример и набор соответствия.
- [Эталонные коннекторы](../docs/engineering/reference-connectors.md): `simulator/` - виртуальные устройства, нагрузка, команды и снимки; `webhook/` - HTTPS-приём событий с долговечными повторами.

```bash
task connectors:conformance
task connectors:conformance OUTAGE_SECONDS=1
task connectors:bench
task connectors:coverage
```

Прямой gRPC-клиент сквозной проверки находится в `tools/event-flow/psim-flow-simulator`; `libs/sim` - отдельный ручной модельный стенд.
