# Audit and Reporting Bundle Size

<!-- language-links -->
[English](audit-and-reporting-assets.md) | [Русский](../ru/engineering/audit-and-reporting-assets.md)
<!-- /language-links -->

Measurements use a fixed Docker build and gzip level 9 for each unique JS file. The initial load includes the selected dictionary. The complete application graph includes dynamic imports; the unique delivery includes the working application and showcase without re-counting shared files.

## Comparison

| Option | Initial EN/RU, bytes gzip | Complete Application Graph, bytes gzip | Unique Delivery, bytes gzip |
|---|---:|---:|---:|
| Audit and Report Placeholders, separate formatters | 119191/120575 | 230857 | Approximately 288160 |
| Lazy Audit and Report Delivery, separate formatters | 118783/120166 | 239851 | 297156 |
| Static Import of Audit and Report | 139537/140920 | 238224 | 295520 |
| Only reachable files of the lazy application | 118783/120166 | 239851 | 239851 |

The initial administrative metrics are taken from the accepted [report](administration-assets.md). Its standard initial threshold uses the standard gzip level; new comparable options use level 9. The last line is a calculation of individual production artifacts per manifest, without changing the current delivery composition and its threshold.

The first implementation imported time formatting through the ICU module. This added approximately 10 KB of parser to the application graph, which was needed for the showcase. The pure Intl formatters are separated from ICU and exported again using the same module, without changing its behavior. Graphs use SVG; no new runtime dependencies. Reading and cancellation transport is separated from the administrative module so that audit does not load administrative metadata.

Static import saves approximately 1.6 KB of complete graph and delivery, but increases the initial load by about 20.8 KB in all roles, still exceeding the previous limit of 294,000 bytes of total delivery. In an intermediate comparison, Terser 5.51.2 increased delivery to approximately 300 KB; the standard minifier was preserved, dependencies and lockfile changes were removed. Lazy separation of languages or individual graphs changes the loading moment, but does not exclude them from the complete graph and unique delivery. Individual artifacts of the application and showcase require independent comparison of delivery composition and quality according to [ADR-070](../adr/0070-lazy-loading-and-delivery-budgets.md).

## Accepted Thresholds

Lazy routes are preserved. The unique JS delivery threshold is set at 300,000 bytes instead of 294,000 bytes, and the complete application graph at 250,000 bytes instead of 240,000 bytes. The initial threshold is 150,000 bytes, the showcase graph is 260,000 bytes, CSS is 15 KB; font thresholds are preserved. The difference from the measured option is approximately 2.8 KB for unique delivery and 10.1 KB for complete graph. The final increase includes both working screens, ru/en, filters, pages, server chain verification, signed checkpoints, authorized downloads, graphs, and tables. The decision is outlined in [ADR-075](../adr/0075-audit-and-reporting-delivery-budget.md) and applied in standard thresholds.

## Re-measurement

`tools/insights/assets.mjs` reads the preserved Vite artifacts `build/insights-assets/lazy` and `build/insights-assets/eager`. Both options differ only in lazy or static import `InsightsPage`; functions and dictionaries are the same. After each option, the entire `dist` with manifest is saved. For measurement, a fixed Node.js development container is used. Standard thresholds run `task web:check`, and the comparative report is saved in `build/insights-assets/comparison.json`.
