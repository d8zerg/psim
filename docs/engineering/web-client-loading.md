# Web Client Loading Comparison

<!-- language-links -->
[English](web-client-loading.md) | [Русский](../ru/engineering/web-client-loading.md)
<!-- /language-links -->

Decisions are [ADR-070](../adr/0070-lazy-loading-and-delivery-budgets.md) and [ADR-076](../adr/0076-web-client-qualification.md). Limits follow [ADR-075](../adr/0075-audit-and-reporting-delivery-budget.md).

## Method

`task web:loading` builds variants using a single fixed tool and runs Chromium against a static server with gzip and real HTTP cache. The original version is taken from `tools/web-quality/baseline.json` via `git archive`; this is a snapshot before qualification. Both the original and current versions use the same dependencies and generated clients. The full repository history must include the specified revision.

Each route/language pair is tested in three new browser contexts. Cold entry uses real files, while re-entry uses the same HTTP cache. A separate series measures the first transition from the console to the module and returning to an already opened module without reloading the document. For cached hashed assets under `/assets/`, `transferSize` is zero. The network series includes JS/JSON from this directory; HTML and root `config.js` are served with `no-store`. Full budgets additionally include root JS files. Raw events contain the URL and compressed size. Previewing the editor and building a report are measured separately from when the module appears.

Routes: `/` is the operator, `/admin` is the administrator, `/integrations` is the integrator, `/audit` is the auditor, `/reports` is the supervisor; transitions are performed with allowed roles. Each is tested for EN and RU. p50 and p95 are published; with three samples, nearest-rank p95 equals the maximum, it’s a local comparison without modeling real channel capacity.

Controlled authentication and API responses are only needed for identical data when measuring files. Business acceptance is performed [separately with real services](web-client-quality.md). The standalone artifact of the showcase opens without `index.html` of the application.

The initial graph uses gzip level 6 as a test; the full graph and unique delivery use gzip level 9 for each file. The full graph traverses static and dynamic imports and related JSON resources. Within the artifact, a file is counted once. JSON dictionaries are included in the size, along with JS, CSS, and fonts, which are tested with their own budgets.

## Variants

| Name | Change |
|---|---|
| `baseline` | Original version with `React.lazy` and `Suspense` |
| `inline` | Selected current configuration: modules by route after role check, dictionaries for both languages within the corresponding module |
| `eager` | Static imports of all modules |
| `widgets` | Separate delayed field container for the administrative editor |
| `selected` | JSON dictionary only for the selected language when opening the module for the first time |
| `bilingual` | Same JSON resources, but both languages are loaded immediately |
| `application` | Only entry from the working console |
| `design` | Only entry from the showcase |

The transformations of alternatives are isolated in the Vite plugin for comparison. The default working configuration is used during build with `inline` without forced selection of a variant. The canonical dictionaries are common to all variants; key matching ensures translation loss is avoided.

## Choice

The overall delivery with delayed modules by route and role check before code loading is preserved. The overall TanStack Query cache only stores module definitions. Server data keeps separate clients and session and scope cleanup. Waiting for `Suspense` when opening a route is replaced with an explicit loading state; measurements confirm that the delay in module display is eliminated.

Static loading exceeds the initial budget. An additional editor container increases the total delivery and delays the first action. Separate dictionaries of the selected language reduce translation transmission when opening, but almost exhaust the overall budget and do not provide a stable time gain. The current variant leaves more space within the current limit. Changing language in the experimental variant waits for all required dictionaries, retains the previous language and draft in case of failure, and allows repetition without reloading.

Separate application build reduces its own artifact, but requires a separate showcase and duplicates common dependencies. Therefore, the cost is fixed for each artifact as the sum of two physical deliveries and as the combination of content by SHA-256. Excluding the showcase from the production image requires a separate solution for delivery composition; with the current choice, both entries remain in the overall build.

No limits are increased. Chunks are not excluded from the full graph. Failure of the first import and old document after updates are tested through explicit page updates and re-entry. A unauthorized route does not load module code. Four recovery checks and all measurements are included in CI.

## Measurements

All sizes are below - bytes gzip. Limits: initial graph 150000 per language, application 250000, showcase 260000, unique delivery 300000.

| Variant | Initial EN/RU | Application full graph | Showcase full graph | Unique JS/JSON | Budgets |
|---|---|---|---|---|---|
| `baseline` | 118952/120336 | 239855 | 184316 | 297160 | pass |
| `inline` | 128466/129859 | 240519 | 183963 | 297812 | pass |
| `eager` | 191964/193357 | 235229 | 182321 | 292379 | initial graph exceeded |
| `widgets` | 129261/130654 | 242075 | 184776 | 299400 | pass |
| `selected` | 128697/130090 | 242157 | 184181 | 299428 | pass |
| `bilingual` | 128713/130106 | 242169 | 184198 | 299441 | pass |
| `application` | 127101/128494 | 238687 | - | 238687 | pass |
| `design` | - | - | 180136 | 180136 | pass |

The full graphs for modules `operator`, `administration`, and `insights` in the selected variant are 240519 bytes each: the traversal includes the overall shell and available dynamic transitions. This is an reachable full graph, not the volume of the first module request. Common files within each graph are counted once.

| Variant | Maximum p95 for cold entry, ms | Warmed entry | First transition | Re-entry | First editor opening, max |
|---|---|---|---|---|---|
| `baseline` | 899 | 851 | 829 | 66 | 43 |
| `inline` | 193 | 89 | 81 | 69 | 48 |
| `eager` | 181 | 74 | 73 | 68 | 43 |
| `widgets` | 154 | 71 | 82 | 71 | 822 |
| `selected` | 162 | 85 | 83 | 71 | 43 |
| `bilingual` | 216 | 75 | 80 | 71 | 42 |
| `application` | 214 | 79 | 76 | 70 | 44 |

Maxima values are taken from all routes and both languages for each variant. Details of the selected configuration: values in the cell are p50/p95, ms.

| Route | Language | Cold Entry | Warm Entry | First Transition | Repeat Transition |
|---|---|---|---|---|---|---|
| `/` | EN | 134/138 | 67/89 | - | - |
| `/admin` | EN | 131/172 | 63/67 | 68/81 | 42/43 |
| `/integrations` | EN | 114/137 | 61/67 | 74/81 | 43/44 |
| `/audit` | EN | 153/179 | 67/67 | 61/69 | 67/68 |
| `/reports` | EN | 127/132 | 59/61 | 64/66 | 68/68 |
| `/` | RU | 128/133 | 74/77 | - | - |
| `/admin` | RU | 124/125 | 63/64 | 69/75 | 43/44 |
| `/integrations` | RU | 122/193 | 63/70 | 71/74 | 42/44 |
| `/audit` | RU | 121/133 | 63/64 | 75/75 | 69/69 |
| `/reports` | RU | 116/130 | 64/65 | 73/76 | 66/68 |

Additional data is provided during the first transition after opening the console, as measured by ResourceTiming (`transferSize`), including browser header evaluation:

| Module | Language | Current Variant, bytes | Selected JSON Dictionary, bytes |
|---|---|---|---|
| `/admin` | EN | 33360 | 30298 |
| `/integrations` | EN | 33360 | 30298 |
| `/audit` | EN | 10417 | 8939 |
| `/reports` | EN | 10417 | 8939 |
| `/admin` | RU | 33360 | 31549 |
| `/integrations` | RU | 33360 | 31549 |
| `/audit` | RU | 10417 | 9613 |
| `/reports` | RU | 10417 | 9613 |

Individual physical deliveries yield 418823 bytes of JS/JSON gzip in total; combining the same content using SHA-256 results in 410187 bytes. This is more than 297812 bytes of the total build. In a separate application, this amounts to 238687 bytes, and in a separate showcase, 180136 bytes; reducing the application does not mean reducing both deliveries together.

In the selected variant, there are 2188 bytes remaining before reaching the total limit, while for `selected`, it’s 572 bytes. Static loading reduces the total size, but still exceeds the initial loading limit for both languages. The delayed editor adds a delay of about 800 ms to the first action; its chunk remains part of the overall graph. The JSON variants have been tested for actual loading of one or both dictionaries; all four JSON files are explicitly excluded from inlining into JS.

Raw data: `build/web-quality/loading.json`, `comparison.json`, `variants.json`, and `.vite/manifest.json` for each variant. The artifacts are under `build/web-quality/variants/`.
