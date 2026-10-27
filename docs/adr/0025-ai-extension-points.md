# ADR-025. AI extension points

<!-- language-links -->
[English](0025-ai-extension-points.md) | [Русский](../ru/adr/0025-ai-extension-points.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-004, ADR-011, ADR-012, [context-map.md](../domain/context-map.md#5-extensions-for-ai-adr-025), X1 in [mvp-scope.md](../product/mvp-scope.md#2-beyond-mvp)

## Context

AI features such as alarm triage, false-positive reduction and an operator assistant are outside MVP scope, but must be addable without changing the core.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Embed model calls in core services | Direct data access | Couples the core to models; model latency and failure affect the hot path |
| Provide no extension points | No current cost | Later integration requires contract and service changes |
| **Define three extension points using existing mechanisms** | No additional MVP code beyond normal contracts; AI modules are external consumers and producers using public contracts | Contract evolution must preserve these extension points |

## Decision

| Extension point | Mechanism | Use |
|---|---|---|
| **Topic consumer** | An external service reads `psim.events.normalized.v1` or `psim.signals.v1` and publishes to `psim.signals.v1` as a registered source rule | A model classifies alarms or produces signals; deterministic `signal_id` follows the same rules |
| **`automated` scenario step** | A signed HTTPS webhook calls an external handler and receives a recommendation, field value or branch choice requiring operator confirmation | Decision support within a scenario |
| **Operator assistant** | Read-only API client with a realtime subscription | Console suggestions and summaries |

Resulting MVP contract requirements:

1. A signal has `origin` (rule or external source) and optional `explanation_ref`.
2. The scenario model includes an `automated` step from MVP, executed as a webhook.
3. Audit identifies AI actions as `actor.kind = SERVICE`, with the module identifier.

## Consequences

- External AI modules follow the same contracts, permissions and audit requirements as other components.

## Verification

Contract tests check that signal and scenario contracts contain these fields.
