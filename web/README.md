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
task web:check      # формат, генерация, ESLint, типы, Vitest с покрытием, сборка, бюджеты бандла
task web:e2e        # Playwright по собранной консоли
task web:dev        # сервер разработки http://localhost:5173
task web:lock       # обновить pnpm-lock.yaml после изменения package.json
```

Node и pnpm на хосте не нужны: команды выполняются в контейнере `node:24` (pnpm 12 через corepack).
