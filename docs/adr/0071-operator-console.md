# ADR-071. Operator’s Work Console

<!-- language-links -->
[English](0071-operator-console.md) | [Русский](../ru/adr/0071-operator-console.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-062](0062-design-system-and-stable-updates.md), [ADR-064](0064-alarm-philosophy.md), [ADR-068](0068-operational-interface-backend.md), [ADR-069](0069-application-shell-and-session-recovery.md), [ADR-070](0070-lazy-loading-and-delivery-budgets.md)

## Solution

The console’s working path uses an existing OIDC session, a REST client, and one realtime channel for the interface. The virtual feed stores row identifiers, focus, and scrolling position during updates. New entries are displayed immediately at the beginning of the list. Filters for text, priority, state, site and assignee are independent. Selection is unaffected by new events or reordering.

TanStack Query stores details of incidents, scenarios, commands, catalogs, object states, and notifications. Changes to incidents, responses, and selected objects invalidate reads; periodic checks restore data between notifications. Only reads are repeated. Changes use a separate Idempotency-Key and the version of the corresponding aggregate. Incident resolution also passes the known version of the response scenario. Conflicts require checking the read state. Changing identity or access scope deletes previous data; failure of current authorization hides previously obtained details.

Mutating actions are unavailable without connectivity or a current successful read. Exercises and video walls in the working path are read-only. Alarm conditions, acknowledgment, and incident lifecycle are independent. Shelving and suppression require a reason and limited time. Sounds are activated explicitly; P1 repeats no more than once every ten seconds, P2 sounds once, P3/P4 don’t sound. Overload limits sound and message frequency in the live region, preserving original records and counters.

Reading an authorized device also returns its type with capabilities. The catalog first checks the tenant and device access scope, then loads the corresponding type in the same transaction. Access to the general type catalog isn’t expanded.

A command is created after previewing a specific device, capability, parameters, step, and consequences. Changing the proposal disables sending until a new preview. Sending uses commandId as an Idempotency-Key and the fingerprint of the preview. After starting the request, re-sending is blocked; unknown results are checked by reading the same commandId. Critical acknowledgment requires another shift supervisor, a fresh preview, and the current version of the command.

Transfer keeps the current responsible person until the recipient acknowledges. The recipient explicitly marks the current version as reviewed of the incident and scenario. Changing the version removes the mark. Original SLA deadlines remain server-side. Shift supervisors assign duty with a separately versioned record and mandatory backup contact.

The floor plan is loaded through an authorized file API in Blob URL; the URL is freed after replacement and object closure. Markers use catalog coordinates and separate projection state. Lack of observation, unknown availability, outdated data, connection loss, and maintenance aren’t shown as normal. Tabular text representation preserves quality, connection, and confirmation time.

## Composition of Browser Code

Current limits are preserved: initial loading up to 150,000 bytes of JS gzip, application graph up to 200,000, showcase dependency graph up to 260,000, total JS up to 294 KB, and CSS up to 15 KB.

Comparison with the original implementation revealed three extra dependencies: server-side protobuf annotations and HTTP descriptors, table during tree import, and showcase dictionaries in the working input graph. Browser generation preserves messages from realtime protocols and enumeration definitions; it excludes server-side custom options and services. Standard field parameters, oneof, map-entry, JSON names, numbers, and time types are preserved. Optimization doesn’t change original contracts or server validation. The tree gets a separate compatible export; the previous export is preserved. Showcase dictionaries are registered only when entering the showcase.

Measured total graph before optimization was 238,260 bytes, after removing annotations it became 224,869 bytes, after separate tree import it became 209,825 bytes, after filtering protocol messages it became 205,379 bytes, after separating dictionaries it became 197,867 bytes. The final console graph was 197,935 bytes. The experimental console build itself remained above the limit. Detailed variations in delivery are described in ADR-070 and ADR-076; local optimizations don’t increase limits or declare research complete.

## Verification

- `task web:check`: Versions, rights, late responses, localization, keyboard, accessibility, visual regression, and budgets.
- `task operator:benchmark`: Separate browser worker, 10,000 records, scrolling frequency and realtime -> DOM delay. This isn’t a measurement of the total input pipeline.
- `task operator:test`: Real PKCE, HTTPS services, Kafka, PostgreSQL, catalog and private plans, Response Engine, Command Service, Connector Gateway and SDK, SLA and notifications.
- `task catalog:test` and catalog runs under ASan/UBSan and TSan: Reading capabilities through an authorized device, prohibiting general type catalog, another site or tenant.
- `task flow:test`: Original event through connector, normalization, correlation and Kafka to browser; separate check of stable effects and tracing.
