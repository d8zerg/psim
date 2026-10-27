# Security Library

<!-- language-links -->
[English](README.md) | [Русский](../../../docs/ru/repository/libs/platform/security/README.md)
<!-- /language-links -->

[ADR-043](../../../docs/adr/0043-platform-security.md). The CMake target is `psim::platform_security`; OpenSSL 3 performs cryptographic operations, while Asio/Beast handles network operations. No new dependencies have been added.

| Interface | Purpose |
|---|---|
| `Secret`, `read_secret` | Protected file `0400`, fd checks, default size up to 64 KiB, buffer cleanup, errors without path/content |
| `TlsOptions`, `tls_context`, `configure_hostname` | TLS 1.3 outside, TLS 1.2+ with AEAD inside, CA/name, mTLS and full fresh CRL |
| `TlsReload` | Atomic publication of new context, saving the previous one in case of error |
| `Peer`, `authenticated_peer` | Identity and SHA-256 only from a completed, verified mTLS-connection |
| `make_csr` | CSR P-256 with limited URI SAN for external PKI |
| `http_request` | HTTPS GET/POST, SNI and DNS/IP verification, one deadline with DNS, header/body limits, no redirects or repetitions |
| `JwtVerifier` | RS256/ES256 via EVP; issuer/audience/type/validity periods; roles of client, tenant, and scopes |
| `JwksCache` | Rotation of JWKS, JWT signature and 500 ms cache introspection; current scope sets can be found at the link |
| `authenticate_service` | mTLS URI SAN, allowed services and JWT; binding `azp` for client credentials |
| `ConnectorTrust` | Registration of certificate fingerprints, overlap of two certificates, sessions are closed upon revocation/expiration, restoration |

## Restrictions and Ownership

JWT cannot exceed 16 KiB; header up to 2 KiB, claims up to 12 KiB, JSON depth up to 32. JWKS cannot exceed 256 KiB and 32 keys, without repeated `kid` and private key material; RSA 2048-8192 bits or P-256. Repeated JSON fields, `none`, HMAC, unsupported `crit`, `jku/jwk/x5u`, incorrect base64url and ID tokens are rejected. Access token has `typ=Bearer`, `iat` and `exp`, duration up to 300 s, with clock skew tolerance up to 30 s. Empty roles and scopes do not grant permission to act.

`JwksCache` and `ConnectorTrust` belong to the same executor; use an instance on a shard. `TlsReload` publishes snapshots between threads; published context cannot be changed. Creating contexts, CSRs, and reading files are control operations, not hot event processing. HTTPS limits the body to 1 MiB by default, headers to 16 KiB; overall deadline is 5 s. Publishing a new context applies to new connections, existing ones retain the previous one until closed.

The default registry limits to 10,000 connectors and sessions, two active certificates per connector, and 128 unexpired fingerprints in total, along with revocations. Revocation closes streams through callbacks after removing session records, so callbacks can safely call `detach`. Callbacks should not throw exceptions. Check `authenticate` on each message, regularly call `expire`. Connector Gateway authorizes registration and recovery, stores registrations, and distributes revisions with acknowledgement from each gateway. Resource Catalog delegates authenticated management requests to it.

## Usage

```cpp
using namespace psim::platform;
security::JwksOptions options{
    .jwt = {.issuer = "https://identity.example/realms/psim", .audience = "psim"},
    .url = "https://identity.example/realms/psim/protocol/openid-connect/certs",
    .introspection_url = "https://identity.example/realms/psim/protocol/openid-connect/token/introspect",
    .introspection_client = "psim",
    .introspection_secret = std::string(secret.view())};
auto cache = security::JwksCache::create(options);
if (!cache) {
  co_return std::unexpected(cache.error());
}
// Cache remains alive on its shard; verify() is awaited on that executor.
auto principal = co_await (*cache)->verify(access_token);
if (!principal) {
  co_return std::unexpected(principal.error());
}
// The owning service now checks the action and access scope.
```

Intermediate HTTP/JWT/OpenSSL errors do not include token, body, password, and key path. `Secret` cannot be formatted or implicitly converted to a string; `view` provides explicit access for the lifetime of the owner. Secrets in external driver memory are not guaranteed to be cleared by this class.

## Adapters and Verifications

Kafka uses internal TLS profile and requires `ClientOptions::security` with CA and protected certificate/key pair or SCRAM-SHA-512 `username/password_file`. Changing native TLS/SASL parameters through `properties` is prohibited. Valkey uses general TLS profile and `crl_file`. PostgreSQL verifies `verify-full` and requires TLS 1.3 by default; the minimum protocol can only be set to TLS 1.2 or TLS 1.3. Schema Registry uses HTTPS; OTLP uses TLS by default and CA from `telemetry.otlp_ca_file`.

```sh
task test:security
task test:security:sanitizers
task test:component
```

The profile `security` runs separate Kafka/PostgreSQL with mTLS and `valkey-tls`, without published ports. Tests confirm real Keycloak tokens, JWKS loading via HTTPS, Schema Registry/mTLS, Kafka publishing and reading, PostgreSQL and Valkey, extending and revoking active streams. Test keys are in an ignored `.cache/`, generation uses only test CAs. Regular local environment datasets are saved.

Explicit exceptions for isolated local environments: Kafka/HTTP `allow_plaintext=true`, PostgreSQL `sslmode=disable`, Redis `tls.enabled=false`, OTLP `telemetry.otlp_tls=false`. Operational procedures and external step-ca are described in [trust schedule](../../../docs/security/platform-trust.md). ClickHouse and domain gRPC/HTTP servers use these mechanisms. Protected deployment and ACL verification are covered by [deployment PKI](../../../docs/engineering/deployment-pki.md) and the [security verification registry](../../../docs/security/threat-register.md).

Working services receive a common configuration through `platform::stream::authentication_options`. Signature-only verification remains a low-level API for autonomous cryptographic checks. Parameters, scope references, and WebSocket are described in [authorization guide](../../../docs/security/authorization.md).
