# Design System and UX Foundation

<!-- language-links -->
[English](design-system.md) | [Русский](../ru/engineering/design-system.md)
<!-- /language-links -->

[ADR-062](../adr/0062-design-system-and-stable-updates.md) defines general solutions. Components are located in `web/packages/design-system`, while settings and dictionaries are in `web/apps/console/src/ui`.

## Viewing

```sh
task web:dev
```

The console is available at `http://localhost:5173/`, and the showcase at `http://localhost:5173/design-system.html`. The showcase contains demonstration data and does not require server services to run. The main page's live feed uses the existing API and real-time protocol.

In the header, you can select theme, density, mode, and language. Themes include dark, light, high-contrast, and night modes; densities include normal, compact, and large for video walls. Normal, crisis, exercise, and video wall modes are selected explicitly. By default, dark theme, normal density, and Russian language are used. All settings apply immediately and are saved; if the storage is unavailable, operations continue within the current page. To test the showcase scenarios, wait for the initial loading to complete.

When changing the language, its dictionaries are loaded first, followed by changes to labels and `html.lang` simultaneously. The current screen remains mounted during waiting and network errors. A retry after a module loading error requires explicit reloading; unfinished operations are not automatically aborted.

## Component Catalog

```sh
task web:catalogue
```

Ladle is available at `http://localhost:61000`. Action, Form, Status, and Data stories show the states of 63 components, including unavailable actions, loading, and errors. Axe is included; interactions are tested by Playwright. The catalog includes four themes, three densities, ru/en, four modes, and an extension for checking string lengths. Identifiers and entered values do not expand. The PSIM mode is located on top of components and does not change the viewing mode of Ladle.

Build checks whether there is a story for named exports of current components. The browser run compares metadata with actual rendered examples and checks form keyboards, command acknowledgments, Escape, and focus return. For each group, 24 visual baselines are saved by theme, density, and language; eight pseudolocalization options are checked separately in a narrow window. Axe checks all four modes in each presentation combination without additional files. The snapshot of the entire page includes bottom components in the story.

`task web:catalogue:check` compares visual baselines, and `task web:catalogue:visual:update` explicitly updates them for viewing. For separate testing, you can pass `GREP`, for example `task web:catalogue:check 'GREP=confirmation|native form'`. Ladle is built separately and is not included in the production console’s budget and delivery.

## Mockups

| Scenario | Dashboard Section | What Can Be Tested |
|---|---|---|
| S2 | Object Catalog | Hierarchical site -> building -> floor -> zone; creating a zone with mapping on the plan, plan with devices and legends, import report with table of rejected lines |
| S6 | Incident Feed | Priorities P1-P4, status and duration, search, filter, selected row, update without replacing the table, separate status of outdated data |
| S7 | Incident Processing | Acceptance, SLA time, scenario steps with current step, timeline with event time |
| S8 | Command to Device | Required basis, acknowledgment with object report, actions and basis, cancellation, waiting for approval from another shift supervisor |
| S9 | SLA and Escalation | SLA violation with norm and elapsed time, escalation chain, backup recipient in case of no duty, keeping the current responsible until recipient agrees |
| S13 | Report | Visible area and period, number, MTTA, MTTR, false alarms, SLA with comparison to previous period, breakdown by priority and export of demonstration CSV |

Scenario codes are shown in navigation and section headers. The connection loss switch is placed in a separate “Mockup Management” section and is not part of the product. These are mockups for testing structures and interactions. Working operations and routing are described in [operator](operator-console.md), [administrator](administration.md), and [audit and reporting](audit-and-reporting.md) guides.

## Tokens and Components

Colors indicate the role of elements. The theme changes token values; components do not choose their own palette.

| Group | Tokens | Purpose |
|---|---|---|
| Surfaces | `background`, `card`, `elevated`, `muted` | Page background, panels, dialogs, table headers, and secondary fillings |
| Text | `foreground`, `muted-foreground`, `link` | Main and secondary text, links |
| Boundaries | `border`, `input`, `ring` | Panel separators, control boundaries and focus indicators with a contrast ratio of at least 3:1 |
| Actions | `primary`, `primary-foreground` | Filling of main actions and text on them |
| Statuses | `danger`, `warning`, `success`, `info` and `<tone>-solid`, `<tone>-solid-foreground` | Text and status icons on any surface; filling of critical actions and markers |

Status has three forms: text color `<tone>`, filling `<tone>-solid`, and derived background mixed with `card`. One value is not used simultaneously for text and filling: light text color in dark theme is not suitable for button background. All text pairs meet WCAG AA 4.5:1 in all four themes. The highlighted table row uses `--selected` and the left column `primary`.

For charts, component roles `chart-series`, `chart-axis`, `chart-grid`, and `chart-selection` are highlighted, related to the theme’s semantics. Regular data series are neutral; values repeat in the table.

Priority P1 is shown with filling `danger-solid`, P2-P4 with dimmed markers. Incident status and priority use different markers to avoid color overload.

Normal density: control height is no less than 40 px, table row is 48 px. Compact: control is 32 px, row is 36 px. Button sizes `sm`, `md`, `lg` are calculated from `--control-height` and maintain proportions in both modes. Sizes are defined by CSS variables; row height can increase for content. Large density uses 64 px control, 80 px row, and increased root font size. On narrow screens, cards switch to one column, the table has its own horizontal scrolling. Fields and their accessible names are not clipped.

- `Button`: `loading` blocks re-launching, preserving name and sizes, including `asChild`; options for main, secondary, dangerous, and additional actions; normal button does not send form without explicit `type="submit"`.
- `Field`, `SelectField`: associated label, hint, and error; `aria-invalid` and `aria-describedby`; external `id` and `aria-describedby` are preserved; `required` is marked in the label. `className` refers to field wrapping.
- `Switch`: switch for setting from `role="switch"`, applied immediately.
- `Badge`: status with text and color; `emphasis="solid"` only for critical markers.
- `Notice`: message with an icon and optional header; errors are available through `role="alert"`, others through `role="status"`. The permanent page content uses `live={false}`.
- `DataRegion`: initial loading, background updates, saved data, and errors; the status line reserves space for three marked messages, considering line wrapping. Measuring elements are hidden from visibility; the message is announced once. The calling page decides which actions are temporarily unavailable.
- `Card`: section with the desired level of header, subheader, description, and actions; `EmptyState` - empty result or lack of access.
- `ConfirmAction`: Radix Alert Dialog, focus on cancellation, Tab within the dialog, Escape and return to the original button. Object summary and consequences are sent to `details`; the unavailable button remains the same DOM node.

Additional entry points:
- `@psim/design-system/controls`: Textarea, Checkbox, RadioGroup, NumberInput, Slider, IconButton, ButtonGroup, Progress, Spinner, Skeleton, DescriptionList, Stat, Timeline, Stepper, Tag, Avatar, and Kbd.
- `@psim/design-system/icons`: named icons and object sets; Icon requires a label or explicit `decorative`, IconButton requires its own label.
- `@psim/design-system/alarm-model`: independent state of condition, confirmation, and alarm visibility; command state machine with approval by another user and prevention of repeated unknown outcomes.

Background updates do not block actions: data remains current. Actions are only blocked when outdated data exists.

## Updates without flickering
1. `theme-init.js` is executed in the head before modules. It checks saved settings fields and sets theme, background, and `html.lang` immediately. React does not switch the initial theme due to late effects.
2. Lines do not disappear with `isFetching` or connection restoration. Before a successful first snapshot, the loading area is displayed; after it, the original table remains with a separate update status.
3. In case of error, saved lines are marked as outdated. Actions are blocked until current data is available; the online color is not used for lost connections.
4. The React key contains a stable ID. The new version updates the existing DOM node; connection status does not recreate cards. The same snapshot retains objects and string arrays.
5. Fixed navigation notifies its height through ResizeObserver in `scroll-padding-top`; switching to the header and keyboard focus remains below it when density changes. Panels have constant space in the layout. Errors are displayed after saved content, without shifting the current table. The theme is switched without overall background animation or page flickering.
6. Gallery collections use TanStack Query structural sharing. Filtered views must not reuse previous data across user, tenant or access-scope boundaries.

Stable rendering still exposes stale data: connection and availability messages update immediately. New incidents may add lines; the virtual working feed retains the anchor of the selected record according to [operator console](operator-console.md) rules.

## Notifications and accessibility
Sound is disabled by default and can be turned on separately. Check buttons allow listening to a short signal and seeing text notifications. Browser audio permission depends on user interaction; if disabled, the message remains visible. Continuous flickering and automatic sound repetition are not used.

Color is not the only indication of state. Each field has a label, each table has a caption, the page has a main header and navigation link. Dialogs maintain focus and return it to the original action. Reduced motion and forced colors are supported. Long identifiers and user names are not used as translatable keys.

## Tests
```sh
task web:check
task web:e2e
task web:a11y
task web:stability
```

Visual regression and accessibility are tested separately:
- Gallery: 24 baselines `#foundations` by theme matrix x density x language and four snapshots of the header with selected mode and CSS effects. Modes do not multiply snapshots of unchanged components.
- Ladle Catalog: 96 baselines for four groups of components by the same matrix and eight snapshots of pseudolocalization. Total in the repository: 132 baselines.
- Axe: Full matrix, including modes, is saved - 96 combinations of gallery and 384 combinations of catalog. Modes switch within one page; each test is a separate test step and does not create snapshots.
- Tests are marked as `@visual` and `@a11y`. Baseline updates run only tests marked `@visual`; `task web:check` tests all scenarios, including keyboard and state saving.

These tests are included in `task web:check` along with behavioral and budget tests. Narrow screens, localStorage prohibition, early application of themes with blocked modules, DOM, focus, and text position during refetch, dialog keyboard, delay, and dictionary loading failure are tested separately. Gallery snapshots are saved in `build/web-design-system`.

## Generation and restrictions
The single source of colors is `web/packages/design-system/tokens/foundation.tokens.json`, structured as [DTCG Format](https://www.designtokens.org/tr/2025.10/format/) and [DTCG Color](https://www.designtokens.org/tr/2025.10/color/). This is a Community Group format, not a W3C standard declaration. The generator creates CSS, TS types and metadata, early `theme-init.js` and local subsets of fonts. Generated content `src/gen` is not stored in Git. `task web:dev` starts generation first.

1152 acceptable pairs of text and surfaces, fillings, and labels, boundaries, and focus are verified in all themes and modes. The main text pairs in the high-contrast theme have a contrast ratio of no less than 7:1; others have a ratio of no less than 4.5:1. Errors in contrast, loops, or missing references stop the generation. Correct states are neutral; the color deviation is replicated by the text. The check without color distinction and forced colors complements Axe; the generator additionally checks the readability of labels during simulations of protanopia, deuteranopia, tritanopia, and achromatopsia. These simulations do not require distinguishing priorities by hue: code and signature remain mandatory.

IBM Plex Sans and IBM Plex Mono are provided locally in WOFF2 format with Latin and Cyrillic fonts, along with licenses. All four Sans and Mono subsets are preloaded before the application first renders. `font-display: optional` preserves the fallback font if loading does not complete before the first display, without having to change metrics later. Table numbers and metrics are tabular.

ICU MessageFormat is used for dynamic number formatting; Intl utilities format numbers, absolute and relative time, lists, and sorting with a specified time zone. Types and checks correspond to keys ru/en. Product and demonstration dictionaries are separated; only the selected language is loaded. Terms and loading agreements are provided in the [glossary](ui-glossary.md).

[ADR-065](../adr/0065-ui-loading-budgets.md) sets the design-system baseline budget of 260 KB JS gzip, with an initial JS budget of 150 KB, CSS of 15 KB, and WOFF2 of 150,000 bytes. The initial budget includes static imports and regular script in HTML. Each exported component undergoes separate size checks with external React/ReactDOM; absence of a record for a new component stops the check. ESLint prohibits literal colors and palette classes in the application; additional checks prohibit separate scales in CSS. General layouts are located in the design system.

Baselines are updated explicitly by the command `task web:visual:update`, which only runs visual tests, and are reviewed after generation. The regular `task web:check` only compares snapshots, not automatically accepting changes. Requirements status is provided in the [approval register](design-system-acceptance.md); implementation is ready for approval. Operator runs, SUS, and measurements with participants are excluded.

## Complex Components and API

The package provides separate imports `@psim/design-system/forms`, `/overlays`, `/navigation`, `/collections`, and `/domain`. The main entry point retains basic components. Radix manages focus, Escape, return to a trigger, and keyboard for dialogs, panels, popovers, tooltips, and menus. `ConfirmAction` uses AlertDialog and sets the initial focus on cancellation.

`Combobox` cancels outdated searches through AbortSignal, retains focus in a field, and virtualizes options. Period selection retains local time and a specified time zone. `FileUpload` accepts transport from the application, blocks late results after cancellation, and displays a report by rows. `ValidatedForm` uses RHF/Zod, error summary, and focus on the first error.

`DataTable` uses TanStack Table and Virtual: stable `id`, sorting, filtering, visibility, and pinning columns, selection, group actions, and grid arrow navigation. Space switches selection, Home/End change a column, Ctrl+Home/Ctrl+End move to the collection boundaries. Inserting records retains selection and scroll anchor. The tree loads descendants by request, retains selection when opened, and supports Arrow/Home/End/Space/Enter. Scalable plans have named markers, legend, and text list; the graph repeats values in the table.

Native controls pass ref and standard properties to the corresponding DOM control; className of a labelled field refers to its container. `Button` supports asChild and ref of a child HTMLElement; without asChild, ref is limited to HTMLButtonElement. A child component must pass ref and events. asChild is not used for combining multiple interactive controls. `Switch`, `Combobox`, `MultiSelect`, `RadioGroup`, `Tabs` and dialogs support controlled value/open/checked with callback and uncontrolled defaultValue/defaultOpen/defaultChecked. The controlled mode does not change during the component’s lifetime.

`data-state`, aria-invalid, aria-busy, and aria-selected reflect state; the truth source is determined by the component’s API. TSDoc describes restrictions on individual controls. Basic geometry and variations are found in CVA recipes with Tailwind. Functional CSS remains for native select, switch geometry, status reservation, tables, forced colors, reduced motion, and load indicators. The application does not duplicate these rules.

## Operational Simulator

The “Open Operational Simulation” button loads the heavy module separately. Seed 42 and logical time ensure scenario repetition: normal steps, 500 alarms, break, queue, recovery, delay, and shift handover. Snapshots feed the feed and plan; changing actions are unavailable in history and when connection is lost. The recovery summary shows the number of events received. Groups retain count, oldest time, highest priority, and access to original records.

The template shows the full model of alarms, shelved and suppressed states with basis, SLA duration, and reassignments, presence, and temporal soft blocking. The shift briefing is accepted by another operator after viewing a specific version; changing data removes the view mark. Previewing a command fixes the object, basis, and consequences; the second confirmation excludes the requestor. Unknown results do not result in re-sending.

Sound is explicitly enabled: different tones P1/P2, P1 repetition limit, volume, and speaker test. The overflow uses an aggregated signal and one live region with a counter not more than once per second. Timers and SLA can be updated separately from the feed. Night theme is scheduled and break reminders are activated by the operator. For multi-monitor placement, one or two columns of panels are available; the video wall uses corresponding mode and density. The exercise mode shows the number of correct responses; crisis is selected explicitly.

The assistant performs local search based on visible facts and displays identifiers and versions of sources. Without suitable sources, it does not form a response. This is a deterministic reading model, without calling an LLM or executing commands. Server behavior is described in the [operational interface guide](operational-interface.md); command delivery and session recovery have their own service guides. The simulator remains isolated from production operations.

## Versioning and Deprecation

Changes to the public API are accompanied by a changeset in `web/.changeset`. `task web:changeset:status` checks the version plan without modifying version files, commits, tags, or publications. The current plan includes minor updates to the design system and realtime client, as well as dependent console patches. Starting versioning and publishing are not part of automatic checks.

Deprecated exports receive TSDoc `@deprecated`, replacement, and migration entries in the changeset. Compatible aliases are retained for at least two minor versions; deletion only occurs during the next major version with a transition description. For experimental APIs, restrictions are explicitly recorded in their TSDoc before being used by applications.

## Reproducible Comparison

`task web:benchmark` runs a separate worker with identical data for both non-virtual and virtual variants. The protocol, results, and limitations are provided in [acceptance testing](design-system-acceptance.md#technical-comparison-protocol). Raw series remain in `build/web-benchmark/results.json`; temporal metrics are checked separately from the main visual matrix, without a parallel browser set.

Current whole-application and distribution budgets are listed in [web client loading](web-client-loading.md).
