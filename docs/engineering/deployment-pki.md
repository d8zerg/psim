# Initial trust and deployment accounts

<!-- language-links -->
[English](deployment-pki.md) | [Русский](../ru/engineering/deployment-pki.md)
<!-- /language-links -->

This procedure applies to [topology delivery](deployment-topologies.md). [ADR-043](../adr/0043-platform-security.md) defines trust domains and certificate requirements. Initial installation creates separate keys, passwords and identifiers; the package contains no preconfigured demonstration accounts.

## External PKI

Copy a suitable profile from `deploy/topologies` and specify actual addresses, failure domains, public DNS name, interface and VIP. Configure DNS and routing for the public address. `security_tenant` defines the technical audit scope; the application tenant is generated when private material is prepared.

```sh
task deploy:render PROFILE=deploy/topologies/cluster.json OUTPUT=build/deployment/customer
task deploy:pki:request BUNDLE=build/deployment/customer PRIVATE=.cache/deployment/customer/material REQUESTS=build/deployment/customer-csr
```

The public bundle contains `material.json` with exact CNs, SANs, URIs, purposes and UIDs. CSRs are stored at `customer-csr/<node>/<role>/<certificate>.pem.csr`. Private keys remain in the `0700` material directory; CSR export transfers only public requests. Repeat the export using `tools/deploy/provision.py export` with a new `--requests` directory.

There are four trust domains: `platform`, `etcd`, `keeper` and `seaweed`. External issuing authorities return public chains named `platform.pem`, `etcd.pem`, `keeper.pem` and `seaweed.pem` in a separate trust directory. Place signed leaf certificates and their intermediate chains in a separate tree using paths from `material.json`, such as `signed/app-1/api-gateway/identity.pem`. Issuing-authority root keys remain in external PKI.

```sh
task deploy:pki:complete BUNDLE=build/deployment/customer PRIVATE=.cache/deployment/customer/material TRUST=build/deployment/customer-trust SIGNED=build/deployment/customer-signed
task deploy:pki:package BUNDLE=build/deployment/customer PRIVATE=.cache/deployment/customer/material PAYLOADS=.cache/deployment/customer/payloads
```

Completion checks key correspondence, chain and current validity, at least one hour of remaining validity, CN, the exact SAN set, `CA:FALSE`, `serverAuth` and `clientAuth`. The trust directory accepts public PEM files. Incomplete or invalid issuance prevents node packaging.

Each archive contains configuration and material for its own node only, numeric UIDs, `0400` permissions and checksums. CA private keys are excluded from node archives. Transfer private-material archives through a secure channel and store them separately from the public bundle and images.

## Initial Keycloak configuration

Installation imports the `psim` realm, public PKCE client `psim-console`, separate confidential clients for introspection and service catalogs, resource-server audiences and fresh secrets. The profile determines the public issuer, which is checked before applications start. `psim_tenant`, `psim_scopes` and `psim_scope_set` are declared managed attributes that require administrative editing.

Create the initial administrator after the installation is ready:

```sh
task deploy:account BUNDLE=build/deployment/customer PRIVATE=.cache/deployment/customer/material USERNAME=platform-admin
```

The command prompts twice for a temporary password of at least 16 characters. The password is passed to the service container through stdin. First login requires a password change and TOTP enrollment. An existing account is preserved and duplicate creation is rejected. Specify additional roles through `tools/deploy/account.py --roles`; the default creates an administrator. Everyday operator accounts receive separate roles and access scopes.

For the local laboratory, use `BUNDLE=build/deployment/lab-cluster` and `PRIVATE=.cache/deployment/lab-cluster/material`. Import the public CA `build/deployment/lab-cluster-ca.pem` into browser trust or provide it to the HTTPS client. Verify the new CA after recreating the laboratory.

## Registering a connector

Create a connector UUID and its own key pair. External PKI issues a client certificate with URI `psim://connector/<uuid>` and `clientAuth`; its chain must be trusted by Connector Gateway. In administration, register the connector with this UUID and public certificate, then create its devices and sources with matching `externalRef` values.

Provide the SDK with the profile's public endpoint on port `7443`, CA, certificate, private key and persistent local buffer directory. Verify connection, event acknowledgments and appearance of a normalized event. `task deploy:flow` exercises this path in its own local laboratory with fresh synthetic credentials, a real issuer, a floor plan, a correlation rule and an incident in the shared console model.

## Renewal and trust replacement

Before expiration, the external issuance process signs a renewal preserving CN, SAN and URI. Existing CSRs allow certificate renewal with the same keys. A new key pair requires a new CSR. Preserve installation identity, tenant, infrastructure passwords and audit signing keys.

Check the chain, validity, EKU, leaf constraints and correspondence to the CSR public key. Compare SANs with `material.json`. Distribute public trust before switching leaf certificates. When changing CAs, install combined old and new chains first, reissue leaf certificates, and finish the transition only after checking every client.

Update one replica at a time: stop the process, replace its certificate and required trust bundle while preserving UID and `0400` permissions, update derived PEM files and Kafka properties, start the process, and check health and a domain request. HAProxy keys and certificates are combined in `public.pem`; Kafka properties contain PEM data and must be updated consistently. Maintain quorum for etcd, Keeper and Patroni. Single-machine operation requires an agreed maintenance window.

Replace a connector certificate through registration rotation with a bounded overlap period. After connection with the new certificate succeeds, revoke the previous fingerprint. Operations are recorded in the audit trail.

The initial installation artifact is immutable. After controlled rotation, retain a separate record of new fingerprints and checks. Reapplying the same node archive does not replace installed material; an archive with different content is rejected. Do not use the initial archive to renew or roll back certificates. Upgrade automation is covered by the [installation lifecycle](../adr/0024-zero-downtime-upgrades.md). Laboratory leaf certificates last two days; the local factory creates fresh issuance when the laboratory is recreated.
