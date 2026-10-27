# ADR-020. Authorization: RBAC, ABAC and the policy decision point

<!-- language-links -->
[English](0020-authorization-model.md) | [Русский](../ru/adr/0020-authorization-model.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-019, [roles-and-journeys.md](../product/roles-and-journeys.md), [aggregates.md](../domain/aggregates.md#6-identity--access)

## Context

Permissions depend on role (operator, shift supervisor, administrator, integrator, auditor or manager) and access scope (sites and zones). Authorization is required for each REST request, realtime filtering at thousands of events per second per instance, and service operations. Decisions must take microseconds; Keycloak supplies roles.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Keycloak Authorization Services (UMA) | Centralized policies | A network call per decision is unsuitable for stream filtering |
| Open Policy Agent (Rego) as a sidecar or service | Expressive policy language | Network calls or Go/Wasm embedding in C++; another subsystem |
| Cedar | Formally verifiable language | Rust implementation requires C++ FFI |
| **A shared policy library: JWT roles, token access-scope attributes, a role × action table and scope checks** | In-process decisions in microseconds; one implementation for API Gateway and services; permissions are data tested as a matrix | Custom code; limited expressiveness, sufficient for MVP |

## Decision

1. Roles are Keycloak client roles; scopes come from Keycloak user or group attributes included in access-token claim `psim_scopes`, a list of sites and zones.
2. Library `libs/platform/authz` exposes `decide(principal, action, resource{site_id, zone_id}) -> allow | deny(reason)`, with deny by default. `contracts/authz/actions.json` defines the role × action table. CMake embeds that same file; policy changes trigger reconfiguration. The web client may use it to hide unavailable actions; server-side enforcement is mandatory.
3. API Gateway checks authorization, and the owning service repeats the check for defense in depth.
4. The library returns a denial code. After rolling back the rejected action, the owning service records `access.denied` through its outbox in a separate transaction. An administrator without the operator role cannot handle incidents.
5. Resource scope includes tenant, site and zone. Supported forms are `*`, a site or zone UUID, and `site:<uuid>` or `zone:<uuid>`. Wildcards bypass neither tenant boundaries nor roles. Introspection with a short cache validates current JWTs and scopes; referenced scope sets update without issuing a new JWT ([ADR-061](0061-live-access-validation.md)). For assignment and transfer acknowledgement, Incident Service separately reads the recipient's current active status, roles and scopes from Keycloak using `view-users` and `view-clients`; browser presence is not used. User-directory failure prevents responsibility changes.

## Consequences

- Console JWTs contain `psim_scope_set`; Keycloak returns user and group scopes through introspection. Inline scopes remain supported for bounded service-identity sets.

## Verification

`task test:authz` runs the role × action × scope matrix, including negative cases. `task arch:authz` checks the shared implementation (FF-22). Configuration and freshness bounds are described in the [guide](../security/authorization.md).
