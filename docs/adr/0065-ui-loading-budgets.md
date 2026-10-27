# ADR-065. Module Loading and Budgets

<!-- language-links -->
[English](0065-ui-loading-budgets.md) | [Русский](../ru/adr/0065-ui-loading-budgets.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-037](0037-web-monorepo.md), [ADR-062](0062-design-system-and-stable-updates.md)
- **Additional:** [ADR-069](0069-application-shell-and-session-recovery.md) - overall delivery of the work shell and interface
- **Additional:** [ADR-070](0070-lazy-loading-and-delivery-budgets.md) - lazy loading options and comparison before budget changes

## Solution

The initial loading of the work console maintains a limit of 150 KB JS gzip. The interactive showcase, component catalog, complex widgets, dictionaries in other languages, and the assistant are loaded separately through individual entry points. The developer catalog is not included in the delivery of the work console.

The total budget for the extended showcase is 260 KB JS gzip. Before the complex widgets, the build occupied 189.17 KB; with virtualized tables, historical rewind, dialogs, command palette, value sources, and the operational simulator, it reached 237.98 KB. The difference of 48.81 KB is due to these entry points and their dependencies. The limit leaves about 9% headroom; the catalog with RHF/Zod, additional menus, and developer forms is delivered separately. The initial loading of the work console is 146170 bytes for en and 147071 bytes for ru, including the selected product dictionary. It maintains a limit of 150000 bytes; other dictionaries and the operational simulator are not included in the initial loading.

CSS is limited to 15 KB; measured at 11.17 KB gzip. Four local subsets of WOFF2 occupy 134564 bytes and are limited to 150000 bytes. Changing the limits requires considering the sizes and reasons; increasing the threshold for hiding unexplained regressions is not allowed.

Component size checks use separate import builds with external React/ReactDOM, without a test environment. Static dependencies of components are taken into account; CSS and fonts are measured separately. The initial console is measured with React, transitive static imports, regular HTML script, and the selected product dictionary. ru and en are checked separately; lazy importing of the selected language does not exclude it from the initial budget. Unused entry points should be removed by the bundler; `sideEffects` lists CSS.

## Verification

The manifest of initial loading, gzip of all delivered files, individual component builds, and a negative run with artificially low limits are tested. The comparison protocol keeps the original and new results. Sizes are published along with the verification results.
