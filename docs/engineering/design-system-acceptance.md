# Design System Acceptance

<!-- language-links -->
[English](design-system-acceptance.md) | [Русский](../ru/engineering/design-system-acceptance.md)
<!-- /language-links -->

## Scope

The acceptance of the design system covers tokens, accessible components, localization, operational scenarios, and automatic checks listed in the requirement registry. Operator runs, SUS, interviews, participant studies, and related basic measurements are not required. MTTA, alarm load, command errors, and shift transfers remain product metrics; their values measured with participants are not replaced by test scenario speed.

Server contracts, storage, and distributed synchronization are described in the [operational interface](operational-interface.md). The model simulates them deterministically. Model readiness does not mean server function readiness.

## DoD

- All requirements in the registry have been implemented and verified within the specified acceptance scope.
- Models cover S2, S6-S9, and S13; automated accessibility checks do not find critical violations.
- Visual regression, semantic tests, and budgets are conducted in `task web:check`.
- Visual checks using available tools have no critical issues; operator acceptance is not required.
- The technical metrics of the prototype are better than the original ones for pre-selected metrics under the same scenarios and environment. Original results, reproduction command, new results, and comparison limits are preserved.
- Results of automatic checks and visual reviews are recorded separately. Unmet requirements are not considered closed due to ADR or test framework.

## Requirement Registry

All requirements below have been met within the design system and deterministic models. Server implementation and verification are documented separately in the service guides.

| Requirement | Status |
|---|---|
| Record the scope and criteria of the design system and implement server support for the operational interface. | Completed |
| ADR “Interface Principles”: Predictability is more important than adaptability, colors only for deviations (ISA-101), adaptability is only offered. | Completed |
| EEMUA 191 / IEC 62682 alarm philosophy: Priorities, complete state model, allowable load, sound policy, shelved and suppressed alarms. | Completed |
| Success metrics and protocol for technical comparison with the original prototype. Operator runs, SUS, and other participant measurements are excluded. | Completed |
| ADR on budget: Lazy entry points, component budgets, new total JS limit. | Completed |
| ADR on font and icons. | Completed |
| ADR on LLM-based assistant: Limits, sources, audit, local operation, command execution prohibition. | Completed |
| Tokens in JSON format according to W3C Design Tokens. Generation of CSS, TS, and colors for theme-init.js, without duplication. | Completed |
| Three levels: basic palette in OKLCH with 12-step scales -> semantics -> component tokens. | Completed |
| Scales: Typography, 4 px grid spacing, radii, shadows, z-index, animation, breakpoints, icon sizes. | Completed |
| Themes: Dark, light, high contrast (AAA 7:1), dim night; full support for forced colors. | Completed |
| Operation mode as token layers: Normal, Crisis, Exercises, Video Wall. | Completed |
| Density: Normal, Compact, Large for video wall. | Completed |
| Contrast test for all token pairs in all themes and modes. | Completed |
| Check the priority palette for achromatism. | Completed |
| Locally hosted font: woff2 with subset of Cyrillic, tabular numbers, monospace variant. | Completed |
| Icons: lucide-react with tree-shaking and object set. Icon without label does not pass type check. | Completed |
| One approach to styles for all components (cva + Tailwind); ds-* classes have been transferred to it. | Completed |
| Patterns from application styles have been transferred to the design system. | Completed |
| Linter rule: Only tokens in the application. | Completed |
| Current defects corrected: scroll-padding-top (WCAG 2.4.11), plural forms. | Completed |
| Component catalog in Storybook or Ladle: Add-on for accessibility, interaction tests. | Completed |
| Visual regression of components by theme, density, and language; separate snapshots of the changeable area for modes. | Completed |
| Semantic tests: Color duplicated with text, actions have names, critical actions require confirmation. | Completed |
| ICU MessageFormat with plural forms, checking for missing keys. | Completed |
| Tools on Intl: Dates, relative time, numbers, lists, sorting, time zones. | Completed |
| Pseudolocalization in visual tests. | Completed |
| Lazy loading of dictionaries, demo strings separated from product strings, glossary ru/en. | Completed |
| Budgets for component size, sideEffects, separate entry points. | Completed |
| Versioning through changesets, API obsolescence policy. | Completed |
| API agreements: ref, asChild, managed and unmanaged modes, data-state, className, TSDoc. | Completed |
| Button with loading state, IconButton with mandatory label, ButtonGroup. | Completed |
| Textarea, Checkbox, Radio, NumberInput, Slider, Switch. | Completed |
| Combobox with asynchronous search and virtualization, MultiSelect. | Completed |
| Selection of date and period with time zone. | Completed |
| Loading file with progress and row report. | Completed |
| Forms on react-hook-form and zod: Error summary, focus on the first error. | Completed |
| Dialog, AlertDialog, Sheet, Popover, Tooltip, DropdownMenu, ContextMenu. | Completed |
| Command palette Ctrl+K. | Completed |
| Side menu for role modules, Tabs, Breadcrumbs, Pagination. | Completed |
| Toast, notification center, Progress, Spinner, Skeleton options. | Completed |
| DescriptionList, Stat, Timeline, Stepper, Tag, user with avatar, Kbd, EmptyState. | Completed |
| WAI-ARIA APG keyboard patterns with tests for each component. | Completed |
| DataTable on TanStack Table and Virtual: Sorting, filters, columns, pinning, selection and group actions, navigation by APG grid pattern, live updates with preserved selected row. | Completed |
| Tree by WAI-ARIA with lazy loading and virtualization. | Completed |
| Floor plan: Scaling, markers, legend, text alternative. | Completed |
| Alarm and command lifecycle as typed finite-state machines. | Completed |
| Check during compilation that each enum in proto has a label. | Completed |
| PriorityBadge, AlarmStateBadge, ConnectionIndicator, SlaTimer, AckButton, CommandLifecycle, AuditReasonField, DeviceMarker, AlarmBanner, camera tile. | Completed |
| Charts: visualization palette, tabular alternative, trends. | Completed |
| Neutral normal state, bright colors only in alerts. | Completed |
| Display of all alert states. | Completed |
| Alert overload: grouping, frequency indicator, mode for 500+ events. | Completed |
| Sound: tones by priority, P1 repetition according to policy, volume, speaker tests, inactive tab. | Completed |
| Time: absolute and relative, time zones, live timers without re-rendering the feed. | Completed |
| Hot keys and their descriptions, stable focus when inserting text. | Completed |
| Layouts for multiple monitors and video walls. | Completed |
| Strategy for live regions during overload. | Completed |
| Crisis mode based on operator decisions. | Completed |
| Honest data: freshness and origin of values as component patterns. | Completed |
| Ergonomic switching: night theme by schedule, reminders about breaks. | Completed |
| Event flow simulator in the interface: deterministic scenarios and control (overload, connection break, delay, change). | Completed |
| State history scrubber over the feed and floor plan. | Completed |
| Summary of changes after a connection break. | Completed |
| Briefing on shift transfer with acknowledgment. | Completed |
| “Why” button in fields with cause chains. | Completed |
| Warning about SLA violation with reassignments. | Completed |
| Preview of command consequences and mobile screen confirmation by the second shift supervisor. | Completed |
| Attention budget panel. | Completed |
| Presence and soft blocking in incident cards. | Completed |
| Indicator for channel delay and degradation. | Completed |
| Interface for exercise evaluation. | Completed |
| Assistant panel with sources. | Completed |

## Implementation Evidence

- DTCG JSON -> CSS/TS/bootstrap: basic OKLCH palette, semantics, component colors, font scales, geometry, shadows, and animations. The generator checks 1152 pairs, including tinted surfaces and selected lines, and simulates color vision deficiencies. Eight negative checks cover contrast, links, types, themes, shadows, and easing.
- CVA/Tailwind define options and basic geometry. The application uses common recipes; functional CSS handles native control features, status reservation, system colors, and reduced motion. Basic, night, high-contrast, and light themes, three densities, and explicitly selected modes are automatically tested.
- Ladle shows 63 components in four groups. The visual matrix contains 96 component snapshots and eight pseudo-localization options; the interface adds 24 base snapshots and four variable area mode snapshots. Total: 132 baselines. Axe saves 480 presentation combinations; modes do not multiply files. Additional scenarios test open overlays and runtime templates.
- Forms: cancellation of asynchronous search, virtualization of Combobox, multiple selection, period with explicit time zone, progress and cancellation of file loading, row report, RHF/Zod, error summary, and focus on the first field.
- Composite widgets: dialogs and Radix panels, popovers, tooltips, menus, Ctrl+K, Tabs, role menus, navigation, notifications. Arrows, Home/End, Space/Enter, Escape, focus cycle, and focus restoration are tested, including the next step after sending commands on the mobile screen.
- Collections: TanStack Table/Virtual, sorting, filters, visibility and pinning of columns, selection and group actions; lazy tree, scalable plan with text list and chart with value tables. Unchanged records retain DOM; value cache takes into account record changes and accessors.
- Object components show the full model of alerts, priorities, connections, SLA, bases, command lifecycle, source and version of values, time and camera without false video stream. All 21 enums have tested TypeScript labels ru/en; adding a new enum stops type checking until labels are added.
- The simulator with seed 42 models 500 event overload, connection break, delay, recovery, and change. History manages the feed and floor plan; changing actions in history and during a break are not available. Groups retain counters and original records. Live region aggregates overload every second; P1/P2 sound requires activation and limits P1 repetitions.
- The template includes recovery summary, shift transfer with viewing of specific version, causes and sources, SLA risk and reassignments, command consequences and second confirmation, attention budget, presence, soft blocking, delay, exercises, night schedule, and reminder about break. The assistant extracts visible facts from sources and does not perform actions.
- ICU, Intl, lazy ru/en, glossary, local WOFF2, and object icons have corresponding checks. Changesets contain minor design system and realtime client and dependent console patch; plan checking does not change versions, commits, tags, and publications. Deprecation policies and API agreements are described in the [guide](design-system.md).
- Principles and limitations of the design system are defined in ADR-062-ADR-067.

The measurements below belong to the design-system acceptance profile. Current whole-application budgets are listed in [web client loading](web-client-loading.md).

## Technical Comparison Protocol

`task web:benchmark` collects Ladle and runs a separate Chromium worker in the fixed Playwright container, with a window size of 1280 x 1000. Both options receive the same deterministic set of 5000 records and three columns of values. The batch changes 500 records. The original version retains the previous non-virtualized display method through map; the new one uses DataTable and additionally includes column selection and control. This comparison is under the same load, not a historical measurement of the entire page.

After five warm-up repetitions, 20 measurements are retained for each action. Time from event to DOM commit is measured using MutationObserver; waiting for a frame between actions is not included in the results. p50 and p95 are determined by the nearest order rank. Full series and summary are retained in `build/web-benchmark/results.json`. The measurement does not include subsequent pixel rendering and does not replace MTTA, SUS, or participant behavior.

| Metric | Original Display | Virtual Table |
|---|---|---|
| Batch update, p50, ms | 12.9 | 8.8 |
| Batch update, p95, ms | 15.6 | 12.3 |
| Response to input, p50, ms | 12 | 3.7 |
| Response to input, p95, ms | 15.1 | 4.9 |
| DOM rows in table | 5000 | 11 |
| Re-creating unchanged records | 0 | 0 |

The test requires improvements in both p50 and p95 time metrics. The rendered table should have fewer than 20 rows, and no re-creations of unchanged records are allowed. Individual browser scenarios confirm that the selected ID and DOM are preserved during insertion, as well as keyboard navigation and state-machine prohibitions. The results are for the local environment; numbers may differ on another machine.

The original templates and styles are saved locally in `build/ux-baseline`. Stability is measured by getting the position of the line using getBoundingClientRect().top + scrollY. The original implementation shifted the selected line 11 px when updating, while the current implementation keeps 0 px, including on narrow screens and high video density. Focus, DOM, and selection are preserved.

The initial JS size was 148520 bytes gzip. The current build with the selected product dictionary occupies 146170 bytes for en and 147071 bytes for ru; for ru, the reduction is 1449 bytes. The initial graph lacks an operational simulator, demonstration lines, and a dictionary in another language. The total JS with extended showcase is 237.98 KB at a limit of 260 KB; CSS is 11.17 KB at a limit of 15 KB, and WOFF2 is 134564 bytes at a limit of 150000. The increase in total JS is explained in [ADR-065](../adr/0065-ui-loading-budgets.md).

## Automatic and Visual Review

`task web:check` passes: 19 tasks, 117 module tests, 12 Node.js checks, and 274 browser tests. All 132 benchmarks are compared; Axe matrix and additional scenarios pass without issues. Line coverage: design system - 93.29%, console - 95.91%; a threshold of 80% is maintained for lines, branches, functions, and statements. `task web:benchmark` and `task web:changeset:status` also pass.

The updated visual baselines are visually reviewed, along with the overflowing operating template and the mobile panel for the second confirmation. Readability, text wrapping, color semantics, accessibility of actions, value source, and focus position are tested; no critical issues are found. Diagnostic images are in `build/web-design-system`. This is a visual review using available tools; operator runs and participant studies were not conducted and are not required.

Server operations and their criteria are described separately: [Contracts and Tests](operational-interface.md).
