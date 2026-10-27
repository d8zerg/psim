# ADR-066. Local Fonts and Object Icons

<!-- language-links -->
[English](0066-ui-font-and-icons.md) | [Русский](../ru/adr/0066-ui-font-and-icons.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-063](0063-interface-principles.md)

## Solution

The interface family is IBM Plex Sans, with IBM Plex Mono for technical values. Local WOFF2 fonts in Latin and Cyrillic are used, without relying on external CDNs. Table numbers and timers use tabular formatting. Loading fonts does not change text selection, focus, or line height; a fallback option ensures readability until the font is loaded. The license is provided along with the fonts.

The basic pictograms are named imports from lucide-react. Importing the entire registry is prohibited; object icons are constructed from a limited set of elements. The semantic Icon requires a accessible label at the type level. The decorative icon requires a clear “decorative” attribute and is hidden from the accessibility tree. IconButton requires its own label, regardless of the icon’s design.

## Verification

The fonts work without an internet connection; Cyrillic characters, tabular numbers, license compliance, and budget are checked. TypeScript rejects Icons and IconButtons without labels; tests confirm that the accessible name is present. Unused icons are not included in the build.

## Sources

[IBM Plex](https://github.com/IBM/plex), [Lucide React](https://lucide.dev/guide/react/).
