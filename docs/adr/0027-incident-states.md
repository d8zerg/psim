# ADR-027. Escalation and false alarms are not incident states

<!-- language-links -->
[English](0027-incident-states.md) | [Русский](../ru/adr/0027-incident-states.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** [state-models.md](../domain/state-models.md#1-incident), [aggregates.md](../domain/aggregates.md#42-aggregate-incident)

## Context

Candidate incident states include new, acknowledged, in progress, resolved, closed, escalated and false alarm. Escalation can happen in any open state: acknowledgement SLA while new, resolution SLA while acknowledged or in progress. False alarm is an outcome that reports need alongside other outcomes.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Seven states including escalation and false alarm | One enum represents every attribute | Escalated loses the current work stage, requiring transitions to every open state; false alarm needs a separate closure path and MTTR handling |
| **Five states, independent `escalation_level` and `resolution`** | Small state machine; escalation preserves work stage and supports levels; one closure path; false-alarm rate uses `resolution` | UI displays escalation and outcome as labels rather than statuses |

## Decision

1. States: `new`, `acknowledged`, `in_progress`, `resolved`, `closed`.
2. Escalation uses `escalation_level`, with 0 meaning none. It changes in open states and produces `IncidentEscalated`.
3. Outcome uses `resolution`: `confirmed`, `false_alarm`, `test` or `duplicate`, assigned when transitioning to `resolved`.
4. A shift supervisor can resolve a `new` incident as `false_alarm`, `test` or `duplicate`, supporting mass false alarms.

## Consequences

- Reports (S13) calculate false-alarm rate as the proportion of incidents resolved during the period with `resolution = false_alarm`.
- The web UI displays escalation and outcome as separate labels; the escalated feed filter uses `escalation_level > 0`.

## Verification

Property-based tests verify aggregate invariants.
