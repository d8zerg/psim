# ADR-062. Design System and Stable Interface Updates

<!-- language-links -->
[English](0062-design-system-and-stable-updates.md) | [Русский](../ru/adr/0062-design-system-and-stable-updates.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-037](0037-web-monorepo.md), [ADR-060](0060-realtime-delivery.md), [ADR-061](0061-live-access-validation.md)

## Context

The operator has been working with change streams for a long time. Replacing the list with a full-screen indicator, resetting the selection, and light-colored backgrounds during startup interfere with operations. General elements should convey priority, communication quality, and operation status equally, supporting the keyboard, both themes, and ru/en.

The implementation uses early settings application, semantic tokens, data retention during background loading, stable row identifiers and dialogs with focus management.

## Solution

1. `@psim/design-system` owns Tailwind CSS 4 tokens, shadcn/ui components, and state patterns. Forms use associated labels and errors, statuses use text and semantic colors. Confirmation is implemented through Radix Alert Dialog with focus trapping and restoration. For simple single-selection, the native select is used. Status colors have separate forms for text and filling: light text color in dark theme is not used as a background for critical actions. Decorative panel borders are separated from control borders, maintaining a 3:1 contrast ratio.
2. Dark theme is applied by default. Theme, density, language, and sound consent are preserved in `psim.ui`. The synchronous external `theme-init.js` applies verified values before application startup and module loading. Unavailable or damaged storage does not block operations. The OS theme does not override the default dark mode.
3. Density changes the height of controls, rows and spacing through common variables. The normal mode is designed for comfortable reading, compact mode for working with large amounts of data. Focus maintains a contrastive outline; reduced motion mode disables movement, forced colors preserve borders.
4. Primary loading shows reserved areas. Reloading preserves current rows and shows separate status. An error does not remove the last successful result; it is explicitly marked as outdated, and actions are blocked by the page owner. Skeleton does not return after each refetch.
5. The row key is the object identifier. Version and connection status do not belong to React keys. Repeated snapshot rows preserve reference identity; feed row is memoized. The height of filter panels and status areas is stable; `scrollbar-gutter` does not allow the page width to change when scrolling.
6. The ru/en language is set with typed dictionaries with separate product namespaces and examples. Selected dictionaries are loaded before the first display. Changing language waits for translation loading, then changes labels and `html.lang` simultaneously, preserving the mounted screen; an error preserves the previous language and input. Identifiers, codes, and user data are not translated. Presentation settings do not contain bearer or server cache data.
7. Notifications always have text and static visual highlighting. Sound is turned off until explicit agreement; a short signal is created by Web Audio after user action. A playback error preserves the visual message. Real notification flow and grouping are connected with the operator console.
8. `design-system.html` is a separate Vite entry point with interactive templates on demo data. It uses common application components. The original budget for the minimal showcase is 180 KB JS gzip and 15 KB CSS gzip; extensions and measurements are described in [ADR-065](0065-ui-loading-budgets.md). Initial loading of the working console is separately limited to 150 KB JS gzip due to Vite manifest transit imports; template and dialog code must not enter it without use. The previous total budget of 150 KB increases due to adding a full showcase, localization, and accessible primitives; initial loading preserves the previous limit. Routing and role modules remain part of the application shell.

## Boundaries

Templates cover S2, S6-S9, and S13 and do not perform working commands or directory changes. The critical command card shows waiting for acknowledgment from another shift supervisor; transferring responsibility awaits recipient agreement. The loss marker of connection is not replaced by the last known online. Interface checks do not replace server authorization.

Saving previous data is only allowed in the previous access context. When changing user, tenant or access scopes, the shell must delete previous cached server data and close subscriptions. `placeholderData` cannot be globally transferred between different access scopes, exposing unauthorized rows.

## Testing

`task web:check`, `task web:e2e`, `task web:a11y`, and end-to-end regression `task flow:test`. Themes, densities, ru/en, contrast, keyboard, and dialog are tested, DOM identity, focus, row position during refetch, and settings application before application modules execute.

## Sources

- [shadcn/ui: Dialog](https://ui.shadcn.com/docs/components/radix/dialog)
- [Radix: Alert Dialog](https://www.radix-ui.com/primitives/docs/components/alert-dialog)
- [TanStack Query: paginated queries](https://tanstack.com/query/latest/docs/framework/react/guides/paginated-queries)
- [Vite: several HTML entry points](https://vite.dev/guide/build.html#multi-page-app)
