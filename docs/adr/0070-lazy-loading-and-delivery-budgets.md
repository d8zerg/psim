# ADR-070. Lazy module loading and Delivery Budgets

<!-- language-links -->
[English](0070-lazy-loading-and-delivery-budgets.md) | [Русский](../ru/adr/0070-lazy-loading-and-delivery-budgets.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-065](0065-ui-loading-budgets.md), [ADR-069](0069-application-shell-and-session-recovery.md)

## Context

Interface budgets must limit the growth of dependencies. Before making any changes to the limit, it’s necessary to compare methods to reduce loading and delivery components. The decisions made remain in effect until comparison results are received.

In the application shell, loading of the feed and protobuf is delayed until a valid session is determined, as well as loading of the operator page and selection of language dictionaries. The current Vite configuration combines the work console and the showcase. The full graph checks for both static and dynamic dependencies; the total gzip budget accounts for all JS files delivered.

Based on the check results, the initial loading is 117631 bytes for EN and 118604 for RU with a limit of 150000. The application’s full graph is 188209 bytes with a limit of 200000, and the showcase is 179486 bytes with a limit of 260000. The total build size is about 270 KB with a limit of 294 KB. The total chunks are included in each user’s input graph, so the sizes cannot be summed to determine the unique delivered size.

## Comparison Options

| Option | Expected Effect | What to Check |
|---|---|---|
| Lazy module loading by route and allowed roles | Less code before opening a specific module | Direct links, first transition, maintaining the same session, no premature preload |
| Lazy loading of heavy widgets, editors, charts, and exports based on actions | Delayed loading of rarely used functions | Delay of the first action, load indicator, failure of chunks and retries |
| Loading only the selected language, then translations of open modules | Less dictionaries on the input route | EN/RU, language switching, absence of text without translation |
| Separate builds and artifacts for the work console and showcase | Excluding view files from the production delivery of the console | Composition of the image, independent use of the showcase, common dependencies and thorough checking of each artifact |
| Removing unused code and unnecessary dependencies, targeted imports | Reducing initial and total volume | Real content of chunks, duplication of libraries, functional and visual regression |

Lazy loading changes the moment code is obtained. If the delivery composition remains unchanged, dividing into chunks does not reduce the total volume. To reduce production artifacts, it’s necessary to exclude unnecessary files from the build and delivery, while maintaining checks on all required functions.

## Selection Process

1. Save initial measurements and dependency composition for both languages and all modules.
2. Compare options based on initial loading, additional loading of the first transition, full graphs of modules, and unique volume of each delivered artifact.
3. Measure delays in cold and warm caches. Check chunk failures, version updates, direct navigation, role changes, and connection restoration.
4. Select an option based on measurement results, and enforce restrictions and checks in CI. Dynamic imports remain part of the full graph; changing the loading moment should not exclude code from measurements.
5. Consider changes to the limit after optimization and comparison of alternatives. Justify the remaining growth with necessary functions, sizes, and impact on delays; document the final decision as a separate ADR.

## Verification and Acceptance

The comparison is completed: [Size, delays, and selection](../engineering/web-client-loading.md). The overall delivery with modules by routes and roles is preserved. The opening delay is eliminated by caching component definitions with explicit loading status. Static imports exceed the initial budget, additional chunks and language resources do not provide a stable advantage, and individual artifacts duplicate dependencies. ADR-075 limits are maintained.

The selected configuration and repeated comparison are stored in CI. The qualification is described in [ADR-076](0076-web-client-qualification.md). New increases in budget require new comparisons, justification of the remaining size, and separate decisions.
