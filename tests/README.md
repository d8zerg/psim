# System Tests

<!-- language-links -->
[English](README.md) | [Русский](../docs/ru/repository/tests/README.md)
<!-- /language-links -->

End-to-end, load, chaos, and contract tests at system level ([Quality Strategy](../docs/quality/strategy.md)). Unit and component tests are placed alongside the code.

[Deterministic simulation](../libs/sim/README.md) is performed separately and manually: `task sim:test`, `task sim:short`, `task sim:long`, `task sim:replay SEED=42`. It is not integrated in CTest and CI (ADR-044).
