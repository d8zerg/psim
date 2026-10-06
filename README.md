# PSIM Platform

Вендор-нейтральная платформа управления ситуациями в физической безопасности (PSIM). Статус: разработка MVP, фаза Ф0 - архитектура и контракты.

- [Документация](docs/README.md) · [план MVP](docs/psim-mvp-development-plan.md) · [прогресс](docs/progress.md)
- [Архитектура](docs/architecture/README.md) · [ADR](docs/adr/README.md) · [контракты](contracts/README.md)

## Разработка

Все действия выполняются задачами [Task](https://taskfile.dev) ([ADR-030](docs/adr/0030-task-runner.md)). Инструменты запускаются в контейнерах с закреплёнными версиями.

Нужно: Docker, Python 3 с PyYAML, Task 3.x.

```
task                  # список задач
task check            # все проверки: контракты и документация
task gen              # перегенерировать сгенерированные файлы
task contracts:check  # только контракты
task docs:check       # только документация
task tools:pull       # скачать образы инструментов
```
