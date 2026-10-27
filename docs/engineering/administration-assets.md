# Administration Bundle Size

<!-- language-links -->
[English](administration-assets.md) | [Русский](../ru/engineering/administration-assets.md)
<!-- /language-links -->

## Composition of Measurements

The build uses fixed Node, pnpm, and Vite in Docker. The gzip of each unique JS file is summed at level 9. The total graph includes static and dynamic imports selected by entrypoint, as well as HTML scripts. The initial load only considers static dependencies and the selected language dictionary. Source maps are not included in the JS budget.

The original measurements from the operator console are: initial load EN/RU - 118094/119478 bytes; total application graph - 197935 bytes; unique distribution - 268.57 KB. Limits are 150000, 200000, and 294000 bytes respectively.

## Comparison of Options

| Option | Initial Load EN/RU, bytes gzip | Total Application Graph, bytes gzip | Unique Distribution, bytes gzip |
|---|---:|---:|---:|
| Lazy Administrative Route with Full Metadata | 119193/120577 | 230061 | 287370 |
| Lazy Route with Uneditable Metadata Removed | 119022/120405 | 229828 | 287136 |
| Static Import of Administrative Module | 159293/160676 | 226902 | 284101 |
| Only Reachable Application Files from Lazy Build | 119022/120405 | 229828 | 229828 |

The last line calculates the composition of a single production artifact based on the manifest, without re-building Vite. It excludes the showcase from the supply but does not reduce the total application graph. The difference in small initial measurements also depends on the gzip level: the standard initial limit uses the standard level; the table uses level 9 for graph comparison.

Static import reduces overhead but exceeds the initial limit. Delayed loading of individual tabs or dictionaries may reduce the first transition; their code will continue to be included in the total graph. Removing uneditable metadata reduced the graph by 233 bytes. No new dependencies are added to the application; the YAML parser is used only for contract generation.

## Accepted Limit

Keep the lazy route and current limits for initial load, showcase, unique distribution, CSS, and fonts. A limit of 240000 bytes gzip is accepted for the total application graph. The remaining growth includes all administrative resources, ru/en, file and version management, rule and scenario checking and publishing. The margin from the measured option is about 10 KB.

The results and limits are accepted according to [ADR-070](../adr/0070-lazy-loading-and-delivery-budgets.md). The limit of 240000 checks the entire application graph. The decision is separately documented in [ADR-073](../adr/0073-administration-delivery-budget.md).

## Reproduction

`tools/admin/assets.mjs` compares the saved Vite artifacts `build/admin-assets/lazy` and `build/admin-assets/eager`. Both options differ only in lazy or static import of the administrative page. The result is `build/admin-assets/comparison.json`. Regular limits continue to be triggered through `task web:check`; final sizes are recorded after functional testing.

## Final Implementation

After clearing cache when switching access scopes and replacing full sets of rules with brief results, the total graph size is 230857 bytes gzip, initial load EN/RU - 119191/120575 bytes, unique distribution - 288.16 KB, CSS - 11.61 KB. All accepted limits are met. The original comparison options are saved separately and are not replaced by the final sizes.

These measurements describe the administration module comparison. Current whole-application limits, including audit and reports, are listed in [web client loading](web-client-loading.md).
