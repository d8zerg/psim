# Trust, Certificates, and Platform Secrets

<!-- language-links -->
[English](platform-trust.md) | [Русский](../ru/security/platform-trust.md)
<!-- /language-links -->

The solution is [ADR-043](../adr/0043-platform-security.md), and the interfaces are [security library](../../libs/platform/security/README.md). The document describes the rules for implementing services and PKI operators. Installation on Debian and VMs is described in [topologies](../engineering/deployment-topologies.md), and administrative CLI is described in [operation and recovery](../engineering/administration-and-recovery.md). Kubernetes is not included in the qualified topologies; static Helm artifacts are described separately in the topology guide.

## Boundaries and Identity

| Connection | Requirements |
|---|---|
| Connector -> Gateway | TLS 1.3, mTLS, EKU clientAuth, URI SAN `psim://connector/<connector-id>`, registered certificate fingerprint |
| Browser -> API | HTTPS TLS 1.3, access token from Keycloak; browser certificate is not required |
| Service -> Service | mTLS, URI SAN `psim://service/<client-id>`, access token of the service or user, allowed list of senders |
| Service -> Kafka/PostgreSQL/Valkey | Verification of CA and name; mTLS or service credentials via TLS; permissions for services |
| JWKS, Schema Registry, OTLP | Configured secure endpoint, verification of CA and name; keys cannot be loaded from URL in the token |

A tenant is defined only by an authenticated principal or certificate registration. The client-provided tenant_id and the URL/SAN without a trusted chain do not grant access. After identity verification, the service checks the action and access scope independently.

## External PKI

For new installations, an external step-ca and step CLI managed by an operator are used. The organization’s existing CA can be used with the same certificate profile. The platform does not store keys from the CA or sign certificates. The root CA is stored separately, issuing intermediate CA certificates; the actual issuance is done by the intermediate CA. To verify revocation at each level, a complete and up-to-date PEM bundle CRL is required.

The operator sets the provisioner and limits allowed URI SAN and roles. A signed CSR confirms ownership of the key, but not the right to use any connector-id, tenant, or service-id. Self-registration with a single certificate is prohibited.

1. Generate a P-256 key on the owner node; set up the service user owner and `0400`. Create a CSR using `make_csr` with the required URI SAN.
2. Sign the CSR using `step ca sign` or corporate PKI. Do not pass the provisioner password or one-time issuance token in the open configuration, log, or diagnostic package.
3. Verify the signature, chain, SAN, EKU, and expiration. The connector certificate is valid for at most 365 days; for automated services, a shorter duration with pre-set renewal is recommended.
4. The authorized operator binds the SHA-256 fingerprint and connector-id to the tenant through Connector Gateway registration management. Resource Catalog can proxy this operation; the persistent registration belongs to Connector Gateway. The fact of trust from the CA does not replace this record.
5. Deliver CA, CRL, and certificates to the nodes, and keep the private key with the owner. Do not deploy certificates from the development `.cache/` to production.

[step CLI sign](https://smallstep.com/docs/step-cli/reference/ca/sign/) and [operation rules for step-ca](https://smallstep.com/docs/step-ca/certificate-authority-server-production/) describe the external tool. The CRL must be enabled and updated separately; automatic expiration of a short certificate does not immediately close the active session.

## Renewal and Revocation

- Issue a new certificate with a new key, same identity, and access scope. Two certificate fingerprints are allowed before the transition ends. No third certificate is added.
- Verify the new material and publish the new TLS context. Errors in reading, mismatch between key and certificate, or incorrect CA leave the previous working context.
- Reconnect the connector using SDK buffer replay and the existing acknowledgement model. The old stream works within the validity period and registration; the application gateway retains credits, order, and deduplication.
- After successful transition, revoke the previous registration, close its sessions on all gateways, wait for acknowledgments, then delete the previous material. Repeated revocation is idempotent.
- In case of compromise, first revoke the registration and close streams, then apply revocation in PKI (`step ca revoke` or corporate tool), update CRL and contexts. Updating the CRL itself does not close an already open stream.
- Verify the expiration on each message and call `ConnectorTrust::expire` on a timer. Expired certificates are prohibited even in previous open connections.

The security library performs 200 consecutive test events through two actual mTLS streams: renewal keeps the old stream, revocation stops reading it, and the new stream continues to work. Verification of the entire pipeline, distributed revocation, and SDK replay is performed by network checks on the gateway and TS-22.

## Access Recovery

Recovery is performed by an authorized operator with MFA. The new certificate must be fresh and approved by external PKI; owning a new key does not replace authorization for recovery. The registry closes all previous sessions and replaces allowed fingerprints, retaining the prohibition of previous certificates until they expire. The reason, initiator, old and new fingerprints are recorded in the Connector Gateway audit. Backup should retain registrations and revocations so that restoring an old backup does not give access to a compromised key.

## Secrets and Rotation

In the configuration, only a link to the file is provided. The file must be ordinary, belong to the service user, and have exact permissions `0400`; symlinks, FIFO, foreign owners, open permissions, and exceeding size are rejected. Failure to read does not reveal the path and content.

Private keys are not passed to JWT, Kafka, logs, trace attributes, or diagnostic packages. `Secret` clears its buffer; drivers store the transferred credentials in their memory. File rotation and verification of a new snapshot are performed on the controlling or blocking executor, then the new state is published. Do not overwrite the existing working key before verifying the new certificate.

For Kafka, `TransportSecurity::password_file` contains only SCRAM passwords, without a line terminator. For configuration secrets, `Config::secret` retains the previous removal of CR/LF. Permissions for topics, schemas, tables, and commands are separately configured for each service in the installer.

## Keycloak Failure and Rotation of JWKS

Issuer, audience, HTTPS endpoint JWKS, and client roles are defined by the operator. Keycloak includes `psim` in the audience access token for consoles and services. In the deployment, each service has its own client credentials and corresponding URI SAN; the common `psim-services` is intended for the local environment.

Caching is updated either on a scheduled basis or based on an unknown `kid` with a limited frequency. A successful response replaces the set of keys, including removing the old ones. If IdP fails, known keys continue to work until `max_stale`, but JWT is still verified against its own `exp`, `iat`, and `nbf`. Unknown keys, outdated caching, and incorrect signatures prohibit access. Revoking a user’s rights before the JWT expires, reauthenticating roles, and other related operations are implemented in the API Gateway and service owners according to ADR-061.

These key-cache rules apply to signature verification. Authorization also requires fresh introspection: a Keycloak outage cannot extend the access-cache lifetime of 500 ms. The JWKS `max_stale` limit does not extend authorization freshness.
