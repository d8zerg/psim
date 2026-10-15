# Коннекторы

[C++ Connector SDK и руководство интегратора](../docs/engineering/connector-sdk.md) реализованы в `libs/platform/connector`. Публичная CMake-цель - `psim::platform_connector`; [пример](../libs/platform/connector/examples/basic.cpp) использует только публичный API. Самостоятельный TLS-сервер набора соответствия находится в `tools/connector-conformance`, запуск внешнего клиента - в `tools/connectors/conformance.py`.

```bash
task connectors:conformance
# Короткий режим для отладки:
task connectors:conformance OUTAGE_SECONDS=1
```

`simulator/` содержит минимальный прямой gRPC-клиент [стенда M1](../docs/engineering/event-flow.md), включая намеренную двойную отправку для проверки идемпотентности конвейера. Полный генератор нагрузки и эталонный Webhook/HTTP-коннектор относятся к шагу 4.3; `libs/sim` остаётся отдельным ручным модельным стендом.
