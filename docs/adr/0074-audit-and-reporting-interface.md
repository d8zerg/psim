# ADR-074. Audit and Reporting Interface

<!-- language-links -->
[English](0074-audit-and-reporting-interface.md) | [Русский](../ru/adr/0074-audit-and-reporting-interface.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-021](0021-audit-hash-chain.md), [ADR-057](0057-incident-reporting.md), [ADR-061](0061-live-access-validation.md), [ADR-069](0069-application-shell-and-session-recovery.md), [ADR-073](0073-administration-delivery-budget.md)

## Solution

The `/audit` and `/reports` routes lazily load the common reading module. Audit is accessible to auditors, reports are for managers and shift supervisors. The current OIDC session and public API Gateway are used; the server rechecks role and access scope for each request.

The audit search uses UTC period, user, action prefix, object type and identifier, site and outcome. Changing the filter resets the cursor and selection of records. The row reveals the original action, registration time, details, and hashes as text. Integrity checking applies to the entire sequence of tenant events, not just the search filter. The interface separately shows the verification result, the first violated sequence, and the presence of a signed external checkpoint. Verification and JSONL export require `*` scope. The export includes the complete snapshot sequence and selection markers for independent verification of selected records.

Reports use server summaries and groupings by site, incident type, priority, responsible person, or UTC day. The client does not average groups or replace server values. uint64 counters are formatted without loss of precision. Missing SLA observations are distinct from zero compliance. A zero duration value alone does not prove measurement. Graphs use SVG and text tables without new libraries; the number of displayed groups is explicitly limited, the full set is available via table pages and CSV.

Edited filters are separated from applied ones. Results and exports are linked to applied parameters; changing fields requires a new request. Automatic request repetition does not occur. Loading, error, empty selection, and network loss have different states. Failure of current authorization hides data. Changing user or effective scope destroys cache and cancels requests, checks, and file loading. General transport only saves tokens in the existing session and prevents using responses from canceled contexts.

Files are downloaded with an authorized request and media type check. Blob URLs are freed when modules are replaced and closed. Audit export creates a new server snapshot of the chain; CSV is built with the same filters but may reflect a more recent projection. The interface does not declare these files as immutable copies of previous pages.

## Verification

Module and browser verifications cover ru/en, accessibility, filters, pages, accurate counters, zero SLA observations, network breaks, and permission revocation. Native acceptance uses actual PKCE with MFA, Kafka, PostgreSQL, ClickHouse, signed checkpoints, and Gateway. Searches and exports are checked, direct changes and record deletion are detected, independent JSONL verification is done, reference metrics for the period and CSV are collected. Active budgets are saved; increases require a separate approved decision.
