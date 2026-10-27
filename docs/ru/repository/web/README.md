# Веб-приложение

<!-- language-links -->
[English](../../../../web/README.md) | [Русский](README.md)
<!-- /language-links -->

Монорепозиторий pnpm и Turborepo ([ADR-037](../../adr/0037-web-monorepo.md)); стандарт - [TypeScript](../../engineering/typescript-coding-standard.md).

| Пакет                      | Назначение                                                                                                                                           |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `apps/console`             | Консоль оператора и администрирование (Vite, React 19, TanStack Router и Query, Zustand, Tailwind CSS 4); оболочка, консоль и административные формы |
| `packages/design-system`   | Токены Tailwind 4 (тёмная тема по умолчанию), `cn`, компоненты в стиле shadcn/ui                                                                     |
| `packages/api-client`      | REST v1: типы из `contracts/openapi/psim-api-v1.yaml`, openapi-fetch, ошибки RFC 9457                                                                |
| `packages/realtime-client` | Realtime-канал (ADR-019): типы из `contracts/proto/psim/realtime/v1`, возобновление, переподключение, `VersionGate`                                  |

Клиенты генерируются из контрактов при каждой сборке в `src/gen` и в git не хранятся.

```
task web:check      # Formatting, generation, ESLint, types, Vitest coverage, build, budgets, visual regression and accessibility
task web:e2e        # Playwright against the built console
task web:dev        # Development server at http://localhost:5173
task web:catalogue  # Ladle component catalogue at http://localhost:61000
task web:benchmark  # Separate large-collection measurement in Chromium
task web:changeset:status # Version plan without changing version files or Git
task web:stability  # Preserve rows, focus and position during updates and connection loss
task web:lock       # Update pnpm-lock.yaml after changing package.json
```

Node и pnpm на хосте не нужны: команды выполняются в контейнере `node:24` (pnpm 12 через corepack).

Консоль использует вход PKCE и ленту инцидентов со снимком и WebSocket-дельтами. Реальный стенд и браузерная проверка - [сквозной стенд](../../engineering/event-flow.md), `task flow:up` и `task flow:test`.

Дизайн-система и макеты доступны в `/design-system.html` после `task web:dev`. Темы, плотность, ru/en, подтверждения и обновление без мерцания описаны в [руководстве](../../engineering/design-system.md). Проверка доступности - `task web:a11y`.

Расширенный объём и исключённые операторские прогоны описаны в [реестре приёмки](../../engineering/design-system-acceptance.md). Визуальные эталоны витрины и каталога обновляются отдельно: `task web:visual:update` запускает только тесты `@visual`; обычная проверка сравнивает эталоны без автоматического принятия и выполняет всю матрицу Axe. Режимы проверяются отдельными снимками шапки витрины, без умножения эталонов компонентов. Каталог можно проверить отдельно через `task web:catalogue:check`. Продуктовые и демонстрационные словари загружаются раздельно; соглашения - в [глоссарии ru/en](../../engineering/ui-glossary.md).

Рабочая оболочка, ролевые маршруты и восстановление сессии описаны в [руководстве](../../engineering/application-shell.md). `task shell:test` проверяет реальный пятиминутный разрыв связи и обновление истёкшего токена.

Администрирование, интеграции и границы доступа описаны в [руководстве](../../engineering/administration.md). `task admin:test` выполняет настройку через браузер с реальными сервисами, PKCE с MFA, импортом, сертификатами, проверкой правил и публикацией сценария.

Полная [квалификация веб-клиента](../../engineering/web-client-quality.md) запускается через `task web:qualify`. Отдельные задачи: `web:loading` - размеры, холодный и прогретый кэш и восстановление чанков; `web:perf` - производительность ленты; `web:native` - сквозные сценарии с настоящими сервисами. [Сравнение вариантов](../../engineering/web-client-loading.md) сохраняет действующие бюджеты.
