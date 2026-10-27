# ADR-022. Multitenant data model with one tenant in MVP

<!-- language-links -->
[English](0022-multitenancy.md) | [Русский](../ru/adr/0022-multitenancy.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-005, ADR-021, [crosscutting.md](../architecture/crosscutting.md#8-multitenancy)

## Context

MVP operates with one tenant. Later, the product must support situation-center federation and multitenant operation (X4) without redesigning data models and contracts.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Omit tenants from MVP | Less code | Adding them later affects every table, key, contract and customer dataset |
| **Shared stores, with `tenant_id` in every entity, message, key and index** | Minimal current cost; enables multitenancy without a model migration | Code provides isolation rather than infrastructure |
| Schema or database per tenant | Strong isolation | Complex migrations and pools; excessive for MVP |
| Installation per tenant | Complete isolation | Needs no model support but does not solve federation |

## Decision

1. Include `tenant_id` (UUID) in every table, as the leading column of composite indexes and primary keys where appropriate; every message envelope; Redis keys (`t:<tenant>:...`); user-token claims; and connector registrations.
2. Derive the tenant only from authenticated context: token, connector certificate or envelope, never request parameters.
3. The MVP installer creates one tenant; no tenant-management API is provided.
4. MVP does not enable PostgreSQL row-level security. Repositories enforce isolation: every query filters by `tenant_id`.

## Consequences

- An extra 16-36 bytes per message and an index column are acceptable.
- Multiple tenants will require a management API, isolation tests and possibly RLS, without changing the model.

## Verification

Fitness function: statically check repository SQL for a `tenant_id` condition; contract linting checks for an envelope containing `tenant_id`.
