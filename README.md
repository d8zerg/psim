# PSIM Platform

Вендор-нейтральная платформа управления ситуациями в физической безопасности (PSIM). Статус: разработка MVP, фаза Ф1 - инженерная платформа.

- [Документация](docs/README.md) · [план MVP](docs/psim-mvp-development-plan.md) · [прогресс](docs/progress.md)
- [Архитектура](docs/architecture/README.md) · [ADR](docs/adr/README.md) · [контракты](contracts/README.md)

## Разработка

Все действия выполняются задачами [Task](https://taskfile.dev) ([ADR-030](docs/adr/0030-task-runner.md)). Инструменты запускаются в контейнерах с закреплёнными версиями.

Нужно: Docker, Python 3 с PyYAML, Task 3.x. Компилятор и зависимости C++ ставить не нужно: сборка идёт в контейнере toolchain ([ADR-032](docs/adr/0032-cpp-toolchain.md)), который собирается автоматически. Его же можно открыть как dev container ([.devcontainer](.devcontainer/devcontainer.json)).

```
task                          # список задач
task ci                       # локальный конвейер - запускать перед каждым коммитом; по области изменения (FULL=true - всё)
task ci:run                   # конвейер на закоммиченном содержимом в чистом клоне (необязательно); ci:status, ci:log
task ci:nightly               # ci + воспроизводимость сборки + сканирование образов
task release                  # ci:nightly + подписанный каталог выпуска dist/ + его проверка
task check                    # все проверки кода (контракты, архитектура, качество C++, тесты, документация)
task gen                      # перегенерировать сгенерированные файлы
task env:up                   # локальное окружение: Kafka, Registry, PostgreSQL, ClickHouse, Valkey, Keycloak, Grafana...
task env:test                 # смоук-тесты окружения (env:down - остановить, env:reset - удалить данные)
task cpp:build                # собрать (PRESET=debug|release|relwithdebinfo|asan|tsan|coverage)
task cpp:test                 # собрать и запустить тесты
task cpp:format               # проверить формат C++ (task cpp:format:fix - исправить)
task cpp:lint                 # clang-tidy по профилю проекта
task cpp:test:sanitizers      # тесты под ASan+UBSan и TSan
task cpp:coverage             # покрытие и пороги по модулям
task arch:check               # fitness functions: зависимости модулей, чистота домена, SQL
task bench:compare            # бенчмарки против базовой линии origin/master (регрессия > 5% - отказ)
task security:deps            # уязвимости и лицензии зависимостей (Trivy, SBOM)
task release:dist             # пакеты DEB, образы, SBOM, подпись (ALLOW_DIRTY=true - пробный выпуск)
task cpp:runtime-test         # запустить собранные тесты на Debian 12 и Astra Linux 1.8
task cpp:reproducible         # проверить воспроизводимость сборки
task toolchain:shell          # оболочка в контейнере toolchain
task contracts:check          # только контракты
task docs:check               # только документация
```

Сборка идёт в контейнере на базе Debian 12 (самая старая целевая glibc), артефакты проверяются запуском на Debian 12 и Astra Linux 1.8. Первая сборка зависимостей C++ занимает десятки минут, затем используется кэш (`.cache/debian12/`).
