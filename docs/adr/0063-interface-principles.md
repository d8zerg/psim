# ADR-063. Principles of Real-Time Interface

<!-- language-links -->
[English](0063-interface-principles.md) | [Русский](../ru/adr/0063-interface-principles.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-062](0062-design-system-and-stable-updates.md)

## Solution

Predictability is more important than adaptability. The order of actions, the identifier of the selected record, focus, and location of main areas remain unchanged during updates. Automatic recommendations can suggest filters, layouts, or crisis modes; the operator applies them explicitly.

A normal state is neutral. Bright colors indicate deviations, risks, or attention needed; focus and links maintain a clear distinction. Color is repeated with text and object symbols. Healthy operation cannot be inferred from the absence of new events: freshness, source, and communication quality are displayed.

Modes: normal, crisis, exercises, video wall. A mode does not change the meaning of commands or expand rights. Changing density and theme does not move working actions. Loss of connection leaves the last successful result with an explicit note; commands based on outdated data are unavailable.

## Verification

Verifications include semantic and keyboard checks, DOM and selected record invariance, contrast, and snapshots of all variants. The presence of status text is checked separately from CSS. Recommendations cannot send commands or apply crisis modes themselves.

## Basis

[ISA101](https://www.isa.org/standards-and-publications/isa-standards/isa-standards-committees/isa101) discusses navigation, charts, colors, and alerts in HMI. The above rules are product policy; the presence of links does not constitute a certification statement according to standards.
