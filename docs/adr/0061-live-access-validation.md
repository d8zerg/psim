# ADR-061. Checking current token validity and scope sets

<!-- language-links -->
[English](0061-live-access-validation.md) | [Русский](../ru/adr/0061-live-access-validation.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-020](0020-authorization-model.md), [ADR-043](0043-platform-security.md), [ADR-060](0060-realtime-delivery.md)

## Context

JWT signature verification alone preserves access until token expiry after a user is disabled. Transferring thousands of zones in a JWT exceeds HTTP header limits. All operation owners must equally check the validity of identity, role, and scope.

## Solution

Services use the common configurations `platform::stream::authentication_options` and `platform::security::JwksCache`: first, local signature and required claims are checked, then introspection is performed through the confidential Keycloak client. The address is set in the configuration. A network request is not made based on unverified token data. Results are cached for up to 500 ms, with 1024 tokens per shard and a 1 s request deadline; an error prevents using previous positive results.

The console sends `psim_scope_set` link to the stable user ID in JWT. Keycloak returns tenant, roles, and full scope in introspection. The server links the response with the JWT signature through subject, issuer, client, tenant, expiry and scope-set identifier. Changed inline claims require token updates; scopes by link are updated without reissue. The protocol does not add a separate storage service for scopes or Keycloak SPI.

Checking role and scope is only done in `platform::authz`, including SQL and ClickHouse filter normalization. Query restrictions are set with parameters. Connector Gateway requires tenant-wide scope for administrative changes; recovery is a separate action.

WebSocket periodically checks validity regardless of Kafka events. Expired trust prevents sending the next message. Changed rights close the connection; the client receives a new snapshot and subscribes again. Explicit reauthentication is preserved.

## Consequences

Keycloak’s unavailability ends access after the short cache expires. This is a deliberate choice for controlled revocation before JWT expiry. Introspection load depends on the number of active tokens per shard; requests from one token are combined. Local signature verification and limited caching protect IdP from queries with unverified tokens.

Settings, revocation limits, and checks are described in the [Authorization Guide](../security/authorization.md).

## Sources

- [Keycloak OIDC endpoints](https://www.keycloak.org/securing-apps/oidc-layers).
- [Keycloak protocol mappers](https://www.keycloak.org/admin-api/protocol-mappers).
