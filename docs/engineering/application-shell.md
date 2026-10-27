# Application Shell

<!-- language-links -->
[English](application-shell.md) | [Русский](../ru/engineering/application-shell.md)
<!-- /language-links -->

Solution: [ADR-069](../adr/0069-application-shell-and-session-recovery.md). The shell combines login, navigation, session state, and realtime recovery.

## Routes

| Route | Module | Navigation Roles |
|---|---|---|
| `/` | Operator Workspace | operator, supervisor |
| `/admin` | Administration | admin |
| `/integrations` | Integrations | admin, integrator |
| `/audit` | Audit | auditor |
| `/reports` | Reports | manager, supervisor |

After entering `/`, a user without an operator module transitions to the first available module. Direct entry retains the selected route; manual navigation to a unavailable module results in an error. Unknown routes retain navigation but show no page. Settings for ru/en, themes, and density remain the same. Navigation focuses on content; stream updates do not shift focus.

Administration, audit, and reporting panels are connected separately. Entry pages indicate module purposes and do not offer simulated functions. The operator stream uses existing REST and realtime protocols. Users without an operator module do not access its APIs or WSS subscriptions. Operator transitions between sections maintain a single channel.

## Session

`window.psimConfig.issuer` sets the exact Keycloak issuer URL without ending with `/`. In production, HTTPS is used; loopback and isolated development environments retain local settings. Public client is `psim-console`, API audience is `psim`, and ID token signing algorithm is RS256. Callback is only allowed for the same origin and route where entry began. Keycloak must allow appropriate redirect URIs and web origins.

Access and refresh tokens are not stored in localStorage or sessionStorage. Reloading a page requires sign-in again through the current Keycloak session. Only verifier, state, nonce, and incomplete PKCE parameters are in sessionStorage; callback removes them before code exchange. Token strings, provider diagnostic responses, and callback parameters are not displayed in error messages.

Refresh combines concurrent requests. Temporary unavailability of Keycloak retains the ability to retry; invalid_grant and incorrect responses end the local session. Exit clears data before Keycloak response and notifies other tabs. If IdP is unavailable, local data remains cleared; the interface reports session termination error.

## Restoration

The channel checks ping/pong availability and updates tokens until they expire. Re-authentication resumes subscriptions from the last applied cursor. Initial retry interval is 500 ms, maximum is 30 seconds, with jitter; the online event triggers an immediate retry. There is no fixed retry count.

Network interruption retains rows and explicitly marks them as last known data. Failure of initial snapshot and ResyncRequired automatically repeat reading; update button speeds up attempts. Rows from old sessions, late HTTP responses, and disconnected socket messages are not used.

`accessContext` cursor links the authorized snapshot with the current server access scope. If it changes, previous rows are cleared until the new snapshot. JWT expiration alone does not change this context and does not require a new snapshot. When the restoration window expires, a new consistent snapshot is also loaded. The server’s [realtime semantics](realtime.md) determine retention and replay budgets.

In the Gateway debug log, event `Realtime connection closed` contains limited reasons for closure. `authorization-refresh-failed` with code indicates verification failure, `authorization-scope-changed` - access scope change, `token-expired` - expiration, `write-timeout` - timeout of sending. Bearer and frame content are not included in this event. Sending awaits verification of rights; outdated rights do not allow queue transmission.

Importing realm and preparing the test environment include rotating refresh tokens: `revokeRefreshToken=true`, `refreshTokenMaxReuse=0`. Preparation updates these parameters in the existing local realm; re-importing the entire realm is not required.

## Checks and Artifacts

- `task web:check` - module-level checks with coverage, routes, and browser accessibility, previous visual regression, loading budgets.
- `task shell:test` - real infrastructure and browser, PKCE, 300,000 ms without network, expired access token, delivery of changes during interruption, and restarting Gateway. `SHELL_PROJECT` sets up a separate Docker Compose project.
- `task realtime:test` - two Gateways, checking cursor context, authentication failures, WSS, retries, and reading Kafka.
- `task realtime:sanitizers` - native checks and real processes under ASan/UBSan and TSan.

`build/event-flow-test/baseline.json` contains duration of interruption and effect comparison, `playwright.log` - browser results. `build/shell-assets.json` contains complete JS graphs and restrictions; initial loading of each language is checked separately. Screenshots and normal test traces contain only data from isolated tests.
