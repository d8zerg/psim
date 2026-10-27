# Системные тесты

<!-- language-links -->
[English](../../../../tests/README.md) | [Русский](README.md)
<!-- /language-links -->

Сквозные, нагрузочные, хаос- и контрактные тесты уровня системы ([стратегия качества](../../quality/strategy.md)). Unit- и компонентные тесты лежат рядом с кодом.

[Детерминированная симуляция](../libs/sim/README.md) выполняется отдельно и только вручную: `task sim:test`, `task sim:short`, `task sim:long`, `task sim:replay SEED=42`. В CTest и CI она не подключена (ADR-044).
