# Authentication and Authorization

<!-- language-links -->
[English](authorization.md) | [Русский](../ru/security/authorization.md)
<!-- /language-links -->

[ADR-020](../adr/0020-authorization-model.md) defines the action table, [ADR-061](../adr/0061-live-access-validation.md) - the validity of rights. The role table is located in `contracts/authz/actions.json`. An administrator without an operator role does not have the right to handle incidents.

Each REST service and administrative gRPC Connector Gateway verifies the signature, issuer, audience `psim`, JWT expiration dates, and validity through a fixed endpoint in Keycloak introspection. The API Gateway passes the original bearer to the operation owner; the owner reverifies and authorizes the action. Unverified role and scope headers are not used.

An authorization denial is stored in the owner’s outbox as `access.denied` with a result of `AUDIT_OUTCOME_DENIED`. Command Service, Response Engine, and Notification Service store the original operation in `details.requested_action`. The Audit Service accepts records based on a stable original identifier.

## Realm and Clients

The isolated test environment configuration is located in `deploy/local/config/keycloak/psim-realm.json`; `tools/env/keycloak_security.py` reconciles the existing realm without removing users or role assignments. `psim-console` is a public client for Authorization Code + PKCE, without a password grant. `psim-services` is a confidential technical client without user login. The confidential `psim` is designed for introspection and storing six roles; token issuance and token exchange through the service account are disabled.

In production, each calling service can have a separate confidential introspection client. Its client ID is additionally included in the audience of the tokens being verified along with `psim`. Service settings:

```yaml
settings:
  issuer: https://identity.example/realms/psim
  jwks_url: https://identity.example/realms/psim/protocol/openid-connect/certs
  introspection_url: https://identity.example/realms/psim/protocol/openid-connect/token/introspect
  introspection_client: psim
  introspection_secret_file: /run/secrets/introspection
  jwks_ca_file: /run/trust/identity-ca.pem
```

The secret file has mode `0400`. The known secret of the local test environment is created by the test environment generator; it is used only in the isolated realm. To change the issuer of the test environment, the reconciler simultaneously sets the frontend URL realm.

## Access Scopes

`psim_tenant` contains one tenant. `psim_scopes` contains `*`, the UUID of the site/zone or `site:<uuid>` and `zone:<uuid>`. Wildcards apply within a tenant and do not add roles. An empty collection does not allow access to resources. Collections are filtered by allowed resources; an empty collection is acceptable. A site scope includes its zones; a zone scope does not grant access to the entire site.

For the console and technical client, the JWT contains `psim_scope_set`, which is equal to the unchangeable Keycloak user ID. User and group scopes are only included in response introspection. The server accepts up to 10,000 scopes, with a response size of up to 1 MB. A mixed JWT with a link and non-empty inline set is rejected. The inline format is supported for limited service identities: changing its assertions requires a new JWT.

Attributes `psim_tenant` and `psim_scopes` can only be changed by the Keycloak administrator. Users cannot expand rights through their profiles. Keycloak Admin API rights are not given to introspection clients. The recipient directory has separate `view-users` and `view-clients`; it checks user and group activity, current roles, and scopes when assigning responsibilities and delivering notifications.

## Revocation

User disablement, session termination, and realm restrictions are verified through introspection. Changing the scopes of the referenced set applies to the previous JWT during the next verification; changing a tenant or incompatible assertions rejects the token. The result is stored for up to 500 ms; errors do not extend previously allowed access. Cache limits 1024 tokens per shard and combines parallel requests for one token. Network deadline is 1 second.

WebSocket reverifies the bearer while idle and before adding a new record. Trust in the result is limited to one second; verification starts 400 ms before its end. When revoked, IdP is unavailable, or roles/scopes are changed, the connection is closed. Under normal conditions, revocation is detected within three seconds, considering network deadline and already started records. Re-authentication with previous rights retains subscriptions; changing rights requires new subscriptions and snapshots. Replay from Kafka is filtered again based on current rights.

Administrative operations on connectors require a role `admin` or `integrator` and scope `*`. Restoration requires separate action `connector.recover`, roles `admin`, and client `psim-console`. The working flow of a connector uses mTLS, registered fingerprints, and certificate revocation independently of user JWTs.

## Verifications

```sh
task test:authz
task authz:sanitizers
task authz:coverage
task arch:authz
```

Policies are verified for each action and all roles with appropriate and foreign sites/zones, an empty scope, a foreign tenant, and expired JWTs. The component set refers to real owners and the public Gateway, changes the reference set without issuing JWTs, and checks the revocation of open WSS connections. Each failure in the outbox of verified services is compared with the original identifier and result in the Audit Service. Verifying the profile confirms changes to the normal field, hiding protected attributes, and inability to change the tenant or grant scope `*` independently.

Diagnostics are stored in `build/authz/<preset>`; tokens and secrets are not recorded in reports. Errors from sanitizers and non-zero service termination codes fail the check. `authz:coverage` starts measurement from an empty profile directory; notifications, 500 WSS clients, and end-to-end flow are executed as separate tasks `notifications:test`, `realtime:test`, and `flow:test`.
