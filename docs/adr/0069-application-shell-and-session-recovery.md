# ADR-069. Application Shell and Session Recovery

<!-- language-links -->
[English](0069-application-shell-and-session-recovery.md) | [Русский](../ru/adr/0069-application-shell-and-session-recovery.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-037](0037-web-monorepo.md), [ADR-060](0060-realtime-delivery.md), [ADR-061](0061-live-access-validation.md), [ADR-065](0065-ui-loading-budgets.md)

## Solution

A shell owns the session and the feed channel independently of the current route. TanStack Router handles links, direct navigation, browser history, unknown routes, and rendering errors. The transition between available modules does not create a new subscription or discard rows. Role-based flags manage navigation; the server independently checks the permissions for each request.

OIDC uses Authorization Code + PKCE S256, a one-time state and nonce, a fixed issuer, and a sign-in transaction limited to ten minutes. The ID token is verified against the JWKS settings in Keycloak: RS256, signature, issuer, audience, azp, expiration, and nonce. The access token subject matches the verified identity. Only the unfinished PKCE transaction is stored in browser storage; access and refresh tokens remain in memory. Refresh is performed with a single session request; Keycloak rotates the refresh token without reusing it. Sign-out immediately clears the rows and session, notifies other tabs, and terminates the session in Keycloak.

The channel updates the token in advance, reconfirms capabilities, and renews subscriptions from the last applied cursor. Network failures use a limited exponential backoff with jitter; the online event accelerates retry. Late responses from old instances, connections, and sessions do not change the current state. When disconnected, the saved rows are marked with the last known data.

Incident feed snapshot and delta cursors contains `accessContext`: SHA-256 canonical subject, tenant, roles, and effective access scopes. JWT expiry and verification time are excluded from the context. The gateway compares the context before renewal. Changing the scope requires ResyncRequired; the client clears previous rows before loading a new allowed snapshot. Old cursors without context maintain compatibility; filtering by the server remains mandatory for any cursor.

Successful JWT cryptographic verification is cached in a JWKS snapshot with a limit of 1024 tokens. The cache keeps the exact token bytes, expiry and backwards clock movement; replacing the snapshot removes the previous cache. Threads synchronize access. Verifying current permissions through introspection is performed separately with previous limits of 500 ms cache and 1 s freshness. Rechecking the signature for each connection does not delay network permission verification during mass login.

The context fingerprint is calculated upon successful authentication and reused for deltas. Mass delivery yields execution after every 32 deliveries to ensure that permission and network responses are processed in time. Frames can wait in a limited queue during permission checking; the sending cycle checks the current identity before transferring and clears the queue if the scope fails or changes. Expiring the previous check when enqueueing a frame does not interrupt ongoing updates. The expiration times and queue limits are maintained.

Authorization failure is sent as a separate fatal error frame; availability errors allow for repetition. Waiting for an error to be sent is limited. Errors 401/403 in the snapshot terminate the local session and clear data. Network errors are not replaced by lack of permissions and do not destroy the recoverable refresh token.

## Delivery Size

The initial budget is 150,000 bytes of JS gzip. Feeds and protobuf are loaded after determining the allowed session. The total size of the work shell, including dynamic imports and dictionaries, is 200,000 bytes; the showcase dependency graph is limited to 260,000 bytes.

The overall build of the work shell and showcase now includes TanStack Router, OIDC verification, and management of the overall session. It occupies about 270 KB instead of 237.98 KB before. The total limit is 294 KB, about 9% above the measured size. This is a separate total limit; initial loading, showcase dependency graph, CSS, and fonts retain their limits. Sizes are checked against the manifest and all delivered files, not excluding new components from measurement.

## Verification

`task web:check` verifies routes, role boundaries, cryptographic input verification, refresh competition, late responses, the overall channel, accessibility and budgets. `task shell:test` verifies real PKCE with Keycloak, network loss for 300,000 ms, updating expired access tokens, restoring changes, and SIGKILL Gateway. Native realtime verifications cover inconsistencies in cursor scopes, authorization failures, and reading from Kafka.
