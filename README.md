# PSIM Platform

Вендор-нейтральная платформа управления ситуациями в физической безопасности (PSIM). Статус: разработка MVP, фаза Ф0 - архитектура и контракты.

- [Документация](docs/README.md) · [план MVP](docs/psim-mvp-development-plan.md) · [прогресс](docs/progress.md)
- [Архитектура](docs/architecture/README.md) · [ADR](docs/adr/README.md) · [контракты](contracts/README.md)

## Разработка

Все действия выполняются задачами [Task](https://taskfile.dev) ([ADR-030](docs/adr/0030-task-runner.md)). Инструменты запускаются в контейнерах с закреплёнными версиями.

Нужно: Docker, Python 3 с PyYAML, Task 3.x. Компилятор и зависимости C++ ставить не нужно: сборка идёт в контейнере toolchain ([ADR-032](docs/adr/0032-cpp-toolchain.md)), который собирается автоматически. Его же можно открыть как dev container ([.devcontainer](.devcontainer/devcontainer.json)).

```
task                          # список задач
task check                    # все проверки: контракты, сборка и тесты C++, документация
task gen                      # перегенерировать сгенерированные файлы
task cpp:build                # собрать (PRESET=debug|release|relwithdebinfo|asan|tsan|coverage)
task cpp:test                 # собрать и запустить тесты
task cpp:runtime-test         # запустить собранные тесты на Debian 12 и Astra Linux 1.8
task cpp:reproducible         # проверить воспроизводимость сборки
task toolchain:shell          # оболочка в контейнере toolchain
task contracts:check          # только контракты
task docs:check               # только документация
```

Сборка идёт в контейнере на базе Debian 12 (самая старая целевая glibc), артефакты проверяются запуском на Debian 12 и Astra Linux 1.8. Первая сборка зависимостей C++ занимает десятки минут, затем используется кэш (`.cache/debian12/`).
