# ADR-073. Budget for the entire application graph with administration

<!-- language-links -->
[English](0073-administration-delivery-budget.md) | [Русский](../ru/adr/0073-administration-delivery-budget.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-065](0065-ui-loading-budgets.md), [ADR-070](0070-lazy-loading-and-delivery-budgets.md), [ADR-072](0072-administration.md)

## Solution

Set the limit for the entire JS graph of the application to 240,000 bytes in gzip format. Keep the initial loading limit at 150,000 bytes, the showcase dependency graph limit at 260,000 bytes, the unique JS delivery at 294 KB, CSS at 15 KB, and fonts. The calculation of the total graph also includes dynamic imports.

[Comparison of options](../engineering/administration-assets.md) shows that the lazy route maintains the initial limit. Static imports reduce the total graph by approximately 3 KB, but still exceed the initial limit. Excluding the showcase dependency graph reduces the unique delivered size while maintaining the size of the application graph. Metadata forms have been cleaned of server fields; no working dependencies have been added.

The results of the comparison and the 240,000-byte limit have been accepted. The initial limit and other checks remain unchanged. Further research on ADR-070 options and testing of production delivery continue to ensure the quality of the web client.
