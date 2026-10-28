# Latency Measurement by Event Time

<!-- language-links -->
[English](latency-measurement.md) | [Русский](../ru/quality/latency-measurement.md)
<!-- /language-links -->

Measurement of latency SLOs for G-02-G-04: event -> signal p99 ≤ 300 ms, event -> console incident p99 ≤ 1 s, REST p99 ≤ 200 ms, and phase budgets ([quality.md](../architecture/quality.md#4-delay-budget)). Measurement uses metrics such as runtime, simulator, and independent cross-checking.

This is the qualification protocol. Available evidence and outstanding qualification requirements are listed in [release readiness](../product/release-readiness.md).

---

## 1. Definitions

| Metric | Start | End |
|---|---|---|
| Event -> Phase | `occurred_at` event (time at source) | Message output from phase (`psim-ts-<stage>`) |
| Event -> Signal | `occurred_at` final event of rule; for `threshold` and `absence` - window closing time | `produced_at` signal, visible to `read_committed` |
| Event -> Console Incident | `occurred_at` event that created the incident | Browser display of the log line |
| REST | API Gateway request acceptance | Last byte of response sent; additionally - client-side load time |

The source time is set by the load generator on its own clock; the generator node is synchronized with platform nodes.

## 2. Clock Synchronization

- All test environment nodes and the generator are synchronized with a single source using chrony; before testing and every 60 seconds, the offset of each node is recorded (`chronyc tracking`).
- Allowable deviation is 5 ms ([acceptance-spec.md](../product/acceptance-spec.md#general-conditions)); if exceeded, the test is considered invalid, not failed.
- Report the largest observed clock offset and the uncertainty from the producer and consumer clocks. A conservative pairwise bound is the sum of their absolute offsets.

## 3. Generating Load Without Distortion

- **Open Model:** The generator sends events according to intensity schedule, not “next after confirmation of previous”. This excludes coordinated omission - reducing delay tails when the system slows down.
- Time `occurred_at` is the scheduled moment, not the actual delivery: if the generator fails, the delay is counted.
- The generator controls its own load; if it falls behind schedule by more than 10 ms, the test is marked invalid.

## 4. Collection

- The qualification protocol requires histograms `psim_stage_latency_seconds{stage}` and `psim_e2e_latency_seconds{stage}` ([quality.md](../architecture/quality.md#44-how-it-is-measured)) in Prometheus native histogram format with a relative error of no worse than 1%.
- For final reports, the generator and reconciliation additionally build HDR histograms from raw data ClickHouse (`occurred_at`, `received_at`, phase times from headers) - with recorded histogram precision, without reconstructing samples from Prometheus buckets.
- Console: In test mode, the browser sends time when the incident line is displayed with `incident_id`; Playwright and the measurement server match it with `occurred_at` of the original event.

## 5. Calculation and Criteria

1. **Warm-up:** Warm-up is performed before the measurement interval and excluded from it: 10 minutes for load tests, or 5% of the measurement duration for soak tests. L-03 requires 10 full minutes of measured load after warm-up.
2. **Windows:** Percentiles are calculated over the entire measurement interval and by sliding windows of 1 min; the criterion is met if it’s fulfilled for the entire interval, and no more than 1% of windows exceed the threshold (short-term spikes are reported).
3. **Chaos Tests:** During failure and recovery, SLO delays are not evaluated; RTO and no loss are measured. After recovery - again SLO.
4. **Update (S12):** SLO is evaluated throughout the entire update without excluding any windows.
5. **Regression:** p50, p99, and p99.9 of each phase are compared with the baseline; an increase of more than 5% blocks merging (PR benchmarks) or release (load tests).

## 6. Report

| Section | Content |
|---|---|
| Conditions | Test environment, versions, profile, seed, duration, clock deviation |
| Capacity | Sent, received, processed by phase; consumer group delays |
| Delays | p50, p90, p99, p99.9, maximum by section 1 and by phases; graph by windows |
| Budget | Phase facts against budget [quality.md](../architecture/quality.md#4-delay-budget) |
| Reconciliation | Result [reconciliation.md](reconciliation.md) |
| Verdict | Completed, not completed, invalid (with reason) |
