# ADR-075. Budget for a unique JS delivery with auditing and reporting

<!-- language-links -->
[English](0075-audit-and-reporting-delivery-budget.md) | [Русский](../ru/adr/0075-audit-and-reporting-delivery-budget.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-065](0065-ui-loading-budgets.md), [ADR-070](0070-lazy-loading-and-delivery-budgets.md), [ADR-073](0073-administration-delivery-budget.md), [ADR-074](0074-audit-and-reporting-interface.md)

## Solution

Set the limit for the unique JS delivery of the application and showcase to 300,000 bytes instead of 294,000 bytes in gzip format. Set the limit for the entire application’s code to 250,000 bytes instead of 240,000 bytes. Maintain the initial limits of 150,000 bytes for the main graph, 260,000 bytes for showcase code, 15 KB for CSS, and fonts. The complete graph continues to account for dynamic imports, while the unique delivery includes all delivered JS files.

[Comparison of options](../engineering/audit-and-reporting-assets.md) shows 297,156 bytes for lazy auditing and reporting, and 295,520 bytes for static imports. The latter increases the initial load by approximately 20.8 KB and also exceeds the previous limit. The unnecessary dependency of the working graph on the ICU parser has been removed; charts add no library, and transport is shared with administration. Preliminary minifier comparisons did not reduce size. The separate delivery of showcase code remains subject to comparison and quality assessment for the web client.

The remaining increase is necessary for working screens, ru/en, filters, and pages, as well as for checking integrity, signed checkpoints, exports, and accessible charts with accurate tables.
