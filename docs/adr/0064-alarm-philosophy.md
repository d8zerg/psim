# ADR-064. Philosophy of Operator Alarms and Load

<!-- language-links -->
[English](0064-alarm-philosophy.md) | [Русский](../ru/adr/0064-alarm-philosophy.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-063](0063-interface-principles.md)

## Solution

An alarm requires a specific action from the operator. Events without such action remain in history and do not become audible alarms. P1 indicates immediate response to critical consequences, P2 high urgency, P3 planned response, P4 low urgency. Priority is determined by consequences and acceptable response time, not by device type.

The status consists of independent conditions and acknowledgments: normal, active-unacknowledged, active-acknowledged, returned-unacknowledged. Operator shelving and suppression according to rules are separate conditions with cause, author, completion time, and audit. Shelving does not mean resolution; returning a condition to normal does not mean acknowledgment. Incident states from the proto retain their own model and are not replaced by this alarm projection.

Original project thresholds for load: warning when more than 6 new alarms occur per hour for the operator, alarm flooding when more than 10 occur in 10 minutes, separate stress scenario with at least 500 events. This is a customizable product policy for the prototype, not a declared standard requirement. The group retains quantity, priority, oldest time, and access to each event. Individual alarms are not lost when limiting the live region’s frequency.

Audible alarms require explicit activation, with volume and speaker testing. P1 repeats at limited intervals until acknowledgment or explicit disconnection of the sound; this does not confirm an alarm. P2 sounds once, P3/P4 without sound. The hidden tab retains a visual counter; browser-based sound prohibition is displayed explicitly. Absence of sound does not change the alarm state. Infinite blinking is not used.

## Verification

Typed state machine, unacceptable transitions, sequences of return and acknowledgment, shelving expiry, limit on sound repetitions, inactive tab, and alarm flooding without loss of counters. Server status owners are implemented separately from the interface.

## Sources

[EEMUA Publication 191](https://www.eemua.org/products/publications/digital/eemua-publication-191) and [IEC 62682](https://webstore.iec.ch/en/publication/65543) provide context for alarm management. For normative verification, full texts must be accessed; this ADR records the interface’s own verified policy.
