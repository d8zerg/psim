# Веб-приложение

Монорепозиторий pnpm и Turborepo (шаг 1.5, [ADR-037](../docs/adr/0037-web-monorepo.md)); стандарт - [TypeScript](../docs/engineering/typescript-coding-standard.md).

| Пакет                      | Назначение                                                                                                                  |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `apps/console`             | Консоль оператора и администрирование (Vite, React 19, TanStack Router и Query, Zustand, Tailwind CSS 4); сейчас - оболочка |
| `packages/design-system`   | Токены Tailwind 4 (тёмная тема по умолчанию), `cn`, компоненты в стиле shadcn/ui                                            |
| `packages/api-client`      | REST v1: типы из `contracts/openapi/psim-api-v1.yaml`, openapi-fetch, ошибки RFC 9457                                       |
| `packages/realtime-client` | Realtime-канал (ADR-019): типы из `contracts/proto/psim/realtime/v1`, возобновление, переподключение, `VersionGate`         |

Клиенты генерируются из контрактов при каждой сборке в `src/gen` и в git не хранятся.

```
task web:check      # формат, генерация, ESLint, типы, Vitest с покрытием, сборка, бюджеты, визуальная регрессия и доступность
task web:e2e        # Playwright по собранной консоли
task web:dev        # сервер разработки http://localhost:5173
task web:catalogue  # каталог компонентов Ladle http://localhost:61000
task web:benchmark  # отдельный замер больших коллекций в Chromium
task web:changeset:status # план версий без изменения файлов версий и Git
task web:stability  # сохранение строк, фокуса и положения при обновлении и потере связи
task web:lock       # обновить pnpm-lock.yaml после изменения package.json
```

Node и pnpm на хосте не нужны: команды выполняются в контейнере `node:24` (pnpm 12 через corepack).

Шаг 3.1 добавляет вход PKCE и минимальную ленту инцидентов со снимком и WebSocket-дельтами. Реальный стенд и браузерная проверка - [M1](../docs/engineering/event-flow.md), `task flow:up` и `task flow:test`.

Дизайн-система и макеты доступны в `/design-system.html` после `task web:dev`. Темы, плотность, ru/en, подтверждения и обновление без мерцания описаны в [руководстве](../docs/engineering/design-system.md). Проверка доступности - `task web:a11y`.

Расширенный объём и исключённые операторские прогоны описаны в [реестре приёмки](../docs/engineering/design-system-acceptance.md). Визуальные эталоны витрины и каталога обновляются отдельно: `task web:visual:update` запускает только тесты `@visual`; обычная проверка сравнивает эталоны без автоматического принятия и выполняет всю матрицу Axe. Режимы проверяются отдельными снимками шапки витрины, без умножения эталонов компонентов. Каталог можно проверить отдельно через `task web:catalogue:check`. Продуктовые и демонстрационные словари загружаются раздельно; соглашения - в [глоссарии ru/en](../docs/engineering/ui-glossary.md).
