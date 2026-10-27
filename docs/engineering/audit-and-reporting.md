# Audit and Reports in the Interface

<!-- language-links -->
[English](audit-and-reporting.md) | [Русский](../ru/engineering/audit-and-reporting.md)
<!-- /language-links -->

`/audit` is accessible to auditors, while `/reports` is available to managers and shift supervisors. The module uses an existing OIDC session and a public Gateway. The solution is [ADR-074](../adr/0074-audit-and-reporting-interface.md); server operations are described in [audit](audit-service.md) and [reports](incident-reports.md).

## Searching for Audit Data

Specify the UTC period, user, action prefix, object type and ID, site, and outcome. The audit period includes both boundaries. By default, the last day is selected. Click “Search Records”: changing the fields does not change the applied filter. Export is not available until re-search. The result shows the applied period; a new search will reset the page.

The list uses an opaque server cursor. The number of records per page is limited by the server, so it may be less than requested even if there’s another page available. The page number is not presented as the total record count. The displayed row shows the original action, object, times, details, and adjacent hashes. Rows are displayed as text.

Auditors with limited access need to explicitly specify the allowed site. Checking the entire chain and exporting to JSONL require `*`; the interface does not offer this for restricted auditors. The server re-checks access and returns a failure for unauthorized sites.

## Integrity and JSONL

The check applies to the sequence of the tenant’s chain regardless of search results. A zero at the beginning indicates the first record, a zero at the end indicates the last one. Numbers are checked as uint64 and sent as strings without conversion to numbers. Negative, overflowing, and reverse ranges are not sent.

The result separately shows the server’s verdict, number of checked records, first violated sequence, and description. The signed checkpoint shows the number, hash, key, signature, tenant, and time. absence of a checkpoint is explicitly indicated. This screen does not perform independent cryptographic checks in the browser.

JSONL creates a new snapshot of the entire tenant chain, including records outside the search filter. A selection flag on records preserves their relationship to the search; the entire chain and external checkpoints are needed for independent verification. If the chain is violated, the export will not create a valid file. Checking the downloaded file:

```sh
psim-audit-tool verify psim-audit.jsonl trusted-keys.json checkpoint-directory
```

Keys and the last record’s external checkpoint are obtained separately from the exported file. For historical snapshots, a consistent copy of the external archive before its last record is required. The native acceptance test takes it from the actual service directory and saves original signatures.

## Manager’s Report

By default, the period is selected as the last 30 days. The report interval is `[from,to)`, not longer than 400 days. Site IDs and incident types are separated by commas or spaces; priorities are selected with a list. Empty filters mean all allowed data, without bypassing the user’s access scope.

Totals and groups by site, incident type, priority, responsible person, and UTC day are available. Creation, acknowledgement, resolution, and SLA have their own observation times. The number of resolutions does not have to equal the number of creations during the period. The group of responsible person uses the last assigned operator.

Totals come from the server and are not averaged. Created and resolved incidents are shown, along with MTTA/MTTR in seconds, false alarms percentage, SLA acknowledgement and resolution compliance, escalated incidents, and SLA denominators. A zero denominator indicates no observations, while zero compliance with a non-zero denominator indicates 0%. Counts are formatted as BigInt. Zero duration does not distinguish zero measurement from no observations.

Charts show the number of created and resolved incidents and MTTA/MTTR times. They use the first 20 groups on the current page; the limit is indicated in the interface. The table contains up to 50 groups per page and all metrics. Chart and table areas can be scrolled with the keyboard. CSV uses applied parameters and the latest server projection; it may differ from the previous screen when the original data is updated.

## Session and Errors

Effective permissions are checked through `/v1/me` upon entry and every five seconds. Changing the user, tenant, roles, sites, zones, or actions creates a new context without previous cache and fields. Responses from canceled contexts are discarded. 401/403 errors hide previous data; restoring access requires explicit action.

Differences include loading, data absence, request error, and network loss. An access-check error other than an authorization denial retains the last result and blocks new requests until the check is restored. When the network is lost, operations are unavailable; the result is marked as potentially outdated. Automatic retries of integrity and export checks do not occur.

The file is read with authorized requests without HTTP cache; the media type is checked before downloading. Blob URLs are freed when the context is replaced and closed. Checking, reading, and exporting are canceled when the session closes. The interface does not include operations to change audits.

## Reproduction

```sh
task web:check
task web:e2e
task insights:test
```

The native acceptance test uses a separate Compose project, real PKCE, and TOTP configuration, HTTPS Gateway, Kafka, PostgreSQL, ClickHouse, and Audit Service with Ed25519. First, ten benchmark incidents over 30 days are checked with MTTA of 60 seconds, MTTR of 120 seconds, false alarms of 20%, acknowledgement SLA compliance of 50%, and resolution SLA compliance of 100%. Through the browser, search, pages, details, JSONL, and its independent verification, precise detection of tampering, and deletion of records with direct SQL, chain restoration, reports within less than three seconds, and CSV with the same metrics are checked. Restrictions on other sites and data hiding after removing roles in Keycloak with the original JWT are checked.

Reports, downloaded files, and logs are saved in `build/insights/<preset>`. The native report is linked to the hash of the actually shown build; changes during the check are rejected. Direct SQL changes are performed only in the isolated test project’s tenant. The check is included in CI for changes to the web client and its tools.
