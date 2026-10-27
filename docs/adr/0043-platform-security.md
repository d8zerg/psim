# ADR-043. Platform Security: TLS, JWT/JWKS, Secrets, and External PKI

<!-- language-links -->
[English](0043-platform-security.md) | [Русский](../ru/adr/0043-platform-security.md)
<!-- /language-links -->

**Status:** Accepted
## Context

The platform includes runtime and transport adapters. For SR-01, SR-03, SR-04, SR-08, and SR-09, uniform trust rules, token verification, and mechanisms for closing revoked sessions are required. ADR-042 requires an external PKI, certificate renewal with overlap, and access restoration. The common library defines trust rules for services and the installer.

## Options

| Option | Properties |
|---|---|
| Verify tokens only in API Gateway | Internal services do not verify trust independently; does not fulfill SR-04 |
| Common library with OpenSSL 3 and verification in each service | Unified TLS policy, signature verification via EVP, tested expiration dates, keys, and revocation |
| Platform-owned CA | Adds key storage, issuance, and operation; conflicts with ADR-042 |
| External step-ca or existing organizational PKI | The platform generates CSRs, the operator manages issuance, trusted CAs, and revocation |

## Solution

1. Add `psim::platform_security`, using already established OpenSSL 3.5.9, Boost.Asio/Beast, and nlohmann_json. Cryptography is performed using OpenSSL; the library includes limited JOSE format adapters and trust policies.
2. On TB1/TB2, allow TLS 1.3; internally, TLS 1.2 and above with AEAD. Always verify the certificate chain and server DNS/IP identity. For mTLS, require client certificates. In general, disable compression, renegotiation, tickets, and TLS session cache. Verify CRL across the entire chain, including the expiration date; an incorrect or outdated list prevents connection.
3. Build a new TLS context before publication. A failed reload preserves the previous context; existing connections retain it until closure. Context reloading does not revoke open streams: this is done through the closure callback.
4. Verify only signed access tokens from Keycloak: RS256 with RSA of at least 2048 bits or ES256 with P-256. Fix issuer, audience, and client roles; verify `sub`, `typ=Bearer`, `iat`, `exp`, and optional `nbf`. Token lifetime must not exceed 300 seconds, and clock tolerance must not exceed 30 seconds. Reject `none`, HMAC, repeated JSON fields, unsupported critical headers, and addresses specified by tokens. Roles should only come from `resource_access[roles_client]`, access scopes from `psim_scopes`, and tenants from `psim_tenant`.
5. Load JWKS only from the configured HTTPS address, without redirection. Limit the body, JSON depth, number of keys, and token size. Successful rotation completely replaces keys; unknown `kid` allows one request per cooling interval. If the IdP is unavailable, the previous set is available for a limited time; an outdated set prohibits authentication. Each shard has its own cache.
6. Inter-service calls require a verified URI SAN `psim://service/<client-id>` from a trusted chain and JWT. For client credentials `azp`, it must match the service; the service is part of the allowed recipient list. The delegated user token retains the user principal. Authorization is repeated by the service owner according to SR-05 for each action.
7. `ConnectorTrust` links the SHA-256 fingerprint to the connector and tenant. At the same time, up to two certificates are allowed. Revocation removes permissions, prohibits re-use of certificates until expiration, and closes corresponding sessions. Restoration requires a fresh certificate and closes previous sessions. Verify the expiration date with each message and timer. Registration, operator rights, storage, audit, and confirmed revocation distribution between gateways are defined in ADR-049; complete the revocation operation only after applying it on all gateways.
8. Secrets are read from files owned by the service user with exact permissions `0400`: open once, verify fd, do not follow symlinks, do not accept directories and FIFO, limit size. Errors do not contain paths or content. `Secret` does not copy and clears its buffer; strings passed to external drivers remain in their memory. References remain in the configuration. For projected volumes, Kubernetes agents or init containers copy material to files owned by the service user with necessary permissions.
9. Schema Registry uses a common HTTPS client; Valkey uses a common TLS profile and CRL. Kafka requires typed mTLS or SCRAM-SHA-512/TLS settings, with secrets from files; unsafe TLS overrides are prohibited. PostgreSQL uses `verify-full`, defaulting to TLS 1.3. OTLP export uses verified TLS and customizable CA. Kafka does not display diagnostic lines: they may contain sensitive data.
10. For external PKI, choose open-source step-ca and step CLI. CA keys are managed by the operator and not provided with the platform. Existing organizational PKIs are compatible when issuing required SAN/EKU and fresh PEM CRLs. Short certificate expiration does not replace immediate revocation of registration. Step-ca has a separate CRL mechanism, which the operator must enable and maintain; the platform needs an up-to-date full bundle. Procedures are in [trust procedures](../security/platform-trust.md); deployment and automation are described in [PKI procedures](../engineering/deployment-pki.md).
11. Isolated development infrastructure retains explicit exceptions: `allow_plaintext=true`, `sslmode=disable`, `telemetry.otlp_tls=false`. Profile `security` verifies real mTLS Kafka/PostgreSQL/Valkey, does not publish ports, and only recreates test nodes. Its certificates and credentials are not used in delivery.

## Verification

`task test:security`, `task test:security:sanitizers`, `task test:component`, `task cpp:test:sanitizers`. Negative JWT/JWK, untrusted CAs and incorrect names, absence and expiration of certificates, fresh/revoked/outdated CRLs, last correct state during rotation, size limit and HTTPS deadline, secrets and real closure of mTLS streams. The token from the actual Keycloak is verified through its JWKS via HTTPS; mTLS Kafka is confirmed by publication and reading.

## Consequences

The library prepares a verified principal but does not define object-level actions. The trust registry works on one executor; catalog snapshots and revocation must be applied to each gateway. Keycloak failure allows verification of already issued tokens based on fresh cache, but not new login. Installation ACLs and closed ports are configured by the installer; full TLS scanning is required for security qualification. ClickHouse TLS adapter is defined in ADR-045.

## Sources

- [JWT Best Current Practices, RFC 8725](https://www.rfc-editor.org/rfc/rfc8725.html).
- [Keycloak OIDC endpoints](https://www.keycloak.org/securing-apps/oidc-layers).
- [OpenSSL: minimum version of TLS](https://docs.openssl.org/3.5/man3/SSL_CTX_set_min_proto_version/).
- [step-ca: usage, including CRL](https://smallstep.com/docs/step-ca/certificate-authority-server-production/).
