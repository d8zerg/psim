# ADR-010. Time semantics: event time, watermarks and clock skew

<!-- language-links -->
[English](0010-time-semantics.md) | [Русский](../ru/adr/0010-time-semantics.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-009, ADR-011, [data-flows.md](../architecture/data-flows.md#33-correlation-psimeventsnormalizedv1---psimsignalsv1)

## Context

Events arrive late and out of order because of SDK buffering during outages of ten minutes or longer, different connectors, and devices moving between zones. Correlation rules use event time for windows. An immediate signal must retain its composition and identifier when the same Kafka log is reprocessed. Source clocks can differ from platform clocks.

## Considered options

| Option | Advantages | Disadvantages |
|---|---|---|
| Processing time | Simple; no waiting | Results depend on delivery delays; history replay differs; buffered retransmission after an outage can produce false correlations |
| Ingestion time at the gateway | Independent of source clocks | The same buffered-retransmission problems |
| **Source event time, watermarks and allowed lateness** | Windows follow event timestamps; window closure bounds waiting | Immediate matches depend on delivery order; relies on source clocks |

## Decision

1. Rules use **event time**, the event's `occurred_at`.
2. **Partition watermark:** the minimum, across active sources in the partition, of `max(source.occurred_at)` minus `allowed_lateness`. Exclude sources with no events for longer than `idle_timeout`, default 30 seconds, so a silent sensor does not stop time progression.
3. **Activation:**
   - `single`, `conjunction` and `sequence` activate immediately when the completing event arrives, without waiting for the watermark.
   - `threshold` and `absence` activate when the watermark closes the window.

   For immediate rules, Kafka processing order determines match composition. If several events qualify, a late event does not modify an already emitted signal. Reprocessing the same log and recovery preserve composition and ID. A historical run sorted by event time may select different evidence for an ambiguous match.

4. **Late events**, whose event time precedes the watermark minus allowed lateness, do not affect closed windows but may affect open windows. Count them in `psim_correlation_late_events_total`. Lateness is configured per rule, defaulting to 10 seconds, with a configuration-defined maximum.
5. **Clock skew:** when `occurred_at` exceeds `received_at` by more than `max_clock_skew`, default 5 seconds, the gateway replaces event time with `received_at`, preserves the original value in an attribute, and emits `system.source.clock_skew` with per-source rate limiting. Future-dated events cannot advance the watermark.
6. **Buffered retransmission after an outage:** events older than `max_buffer_replay_age`, default one hour, are normalized and retained in history but marked late for correlation, preventing large numbers of false signals from old events.
7. Absolute time for `absence` at the window boundary is computed from the watermark. Historical runs use the same absolute boundaries but do not model delivery or exclusion of late events from already closed windows.
8. Domain-core time is accessed only through `Clock`; system clocks remain in adapters.

## Consequences

- Rules can trade completeness against latency.
- `absence` and `threshold` latency equals the window plus allowed lateness. Latency budgets measure them from window closure.
- Platform nodes synchronize through chrony; clock-skew metrics monitor nodes.

## Verification

Deterministic domain-core tests cover time boundaries and permutations with unambiguous matches. Immediate-rule tests cover delivery-order dependence of ambiguous match composition. Replaying the same log, failure and recovery preserve the signal set and IDs. Historical runs are compared with live processing on unambiguous data within open windows.
