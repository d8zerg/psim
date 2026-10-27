# Threat and security requirement register

Generated from [threat-model.yaml](threat-model.yaml) by `task docs:threats:gen`. Do not edit manually.
Method, assets and trust boundaries: [threat-model.md](threat-model.md).

## 1. Summary

| Risk | Threats |
|---|---|
| High | 30 |
| Medium | 16 |
| Low | 3 |
| **Total** | **49** |

Security requirements: 38.

## 2. Threats by boundary and STRIDE category

| Boundary | S | T | R | I | D | E |
|---|---|---|---|---|---|---|
| TB1 Connector -> Connector Gateway | T01 | T02, T03, T09, T43 | T04 | T05, T10 | T06, T07 | T08 |
| TB2 Browser -> API Gateway | T11, T12 | T15, T16, T21, T44, T48 | T17, T45 | T19 | T18, T42 | T13, T14, T20, T49 |
| TB3 Service <-> Service | T22 | T23 | - | T24 | - | - |
| TB4 Services -> Storage | - | T25, T29 | - | T26, T30 | T28 | T27 |
| TB5 Administration | - | T31 | T32 | T34 | - | T33, T46 |
| TB6 Platform -> External Systems | T37 | - | - | T36 | T38 | T35, T47 |
| TB7 Supply chain | - | T39, T40 | - | - | - | T41 |

## 3. Threats

Ordered by descending risk. Risk = likelihood × impact.

| ID | Boundary | STRIDE | Threat | Risk | Controls | Residual risk |
|---|---|---|---|---|---|---|
| T06 | TB1 | D | **Connector event flood.** A faulty or malicious connector exceeds event limits and displaces other sources. | high (3×3) | SR-16, SR-18, SR-27 | Each connector's throughput remains bounded by its configured limit. |
| T11 | TB2 | S | **Credential theft or guessing.** An operator, administrator or shift supervisor password is phished or guessed. | high (3×3) | SR-02, SR-26 | MFA is optional for the operator role and configured by the customer. |
| T01 | TB1 | S | **Connector impersonation with a stolen certificate.** An attacker with a connector certificate connects and sends events on behalf of that connector. | high (2×3) | SR-01, SR-10, SR-26 | Until revocation, the attacker can send events for the connector's sources; session audits and anomaly detection help identify this. |
| T02 | TB1 | T | **False events from a compromised connector.** A connector generates false alarms to distract operators or suppresses real events. | high (2×3) | SR-01, SR-16, SR-27, SR-30 | Plausible false events cannot be distinguished at the protocol level; correlate multiple sources and monitor sequence gaps to reduce the risk. |
| T03 | TB1 | T | **Interception and modification of connector traffic.** An attacker in the site's network modifies events or commands in transit. | high (2×3) | SR-01, SR-08 | None with correct TLS configuration. |
| T07 | TB1 | D | **Malicious protocol messages.** Oversized or malformed messages crash the gateway. | high (2×3) | SR-11, SR-12 | Address parsing vulnerabilities in third-party libraries (gRPC, protobuf) by updating dependencies. |
| T12 | TB2 | S | **Access token theft.** A token is extracted through XSS, proxy logs or browser history. | high (2×3) | SR-03, SR-14, SR-15 | A stolen token is valid for no longer than 5 minutes. |
| T13 | TB2 | E | **Access to another site's incidents.** An operator supplies the identifier of an incident, device or zone outside their access scope. | high (3×2) | SR-05 | None with a complete authorization test matrix. |
| T14 | TB2 | E | **Operator execution of administrator actions.** Administration methods are called with an operator role token. | high (2×3) | SR-05, SR-10 | None with a complete authorization test matrix. |
| T16 | TB2 | T | **Script injection through connector and user data.** Event attributes, device names or comments contain HTML or scripts that execute in the console. | high (2×3) | SR-11, SR-14 | None when output rendering rules are followed. |
| T17 | TB2 | R | **Operator repudiation of performed actions.** An operator denies accepting an incident, skipping a step or sending a command. | high (2×3) | SR-10, SR-02 | Actions performed under another person's compromised account (see T11). |
| T20 | TB2 | E | **Abuse of critical commands by one employee.** An operator or shift supervisor unlocks many doors or disarms a site. | high (2×3) | SR-06, SR-10, SR-26 | Collusion between two employees remains possible and is detectable through audit. |
| T22 | TB3 | S | **Service impersonation on the internal network.** An unauthorized process calls service APIs while bypassing API Gateway. | high (2×3) | SR-04, SR-19 | Compromise of an application node exposes the keys of services running on that node. |
| T23 | TB3 | T | **Writing to another service's topics.** A compromised service publishes forged signals, incidents or commands. | high (2×3) | SR-19 | A service can forge messages in its own topics. |
| T25 | TB4 | T | **Database administrator modification of the audit log.** An administrator with superuser privileges changes or deletes audit records. | high (2×3) | SR-10, SR-26 | Integrity verification detects changes but cannot prevent them. |
| T27 | TB4 | E | **Injection into PostgreSQL and ClickHouse queries.** Search filters or event attributes alter query semantics. | high (2×3) | SR-13, SR-12 | None when parameterized query rules are followed. |
| T28 | TB4 | D | **Storage disk exhaustion.** Event traffic or growing history fills Kafka, PostgreSQL or ClickHouse disks and stops the pipeline. | high (2×3) | SR-18, SR-27 | Incorrect sizing requires operations staff to intervene before disks fill. |
| T30 | TB4 | I | **Exposed storage without authentication.** Redis, ClickHouse, Kafka or PostgreSQL are reachable from user or site networks. | high (2×3) | SR-19, SR-08 | Customer firewall misconfiguration; checked by the installer. |
| T31 | TB5 | T | **Suppressing response through configuration changes.** An administrator disables a fire alarm rule or changes a mapping so that alarms become info events. | high (2×3) | SR-07, SR-10, SR-26 | Subtle rule condition changes without disabling the rule are detectable only by reviewing audited changes. |
| T34 | TB5 | I | **Secret leakage through configuration, logs and diagnostic bundles.** Passwords and keys appear in logs, traces or support bundles. | high (2×3) | SR-09 | The node's OS administrator can access secrets in process memory. |
| T35 | TB6 | E | **SSRF through webhook and automatic step addresses.** An administrator or an attacker with administrator privileges specifies an internal service or metadata address and gains access to the internal network. | high (2×3) | SR-20, SR-07 | An allowlisted destination may be compromised on the recipient's side. |
| T39 | TB7 | T | **Compromise of dependencies and build tools.** A malicious library version or tooling image enters the build. | high (2×3) | SR-23 | Compromise of a pinned version before its publication. |
| T42 | TB2 | D | **Malicious floor plan file or access to another site's object.** An uploaded image causes excessive memory consumption, or an attacker bypasses the API to read another site's floor plan through a public bucket. | high (2×3) | SR-31, SR-11, SR-06 | None with validation, limits and private storage access. |
| T43 | TB1 | T | **Incorrect current state after reconnection.** Old buffered events or a snapshot overwrite a recent alarm and incorrectly show the device as healthy. | high (2×3) | SR-32, SR-10 | Sources without snapshots remain unknown until a confirmed observation. |
| T44 | TB2 | T | **Alarm concealment through maintenance mode.** A user enables suppression for another zone or leaves a device in maintenance without an end time to hide alarms. | high (2×3) | SR-33, SR-10 | An administrator may suppress policy-permitted signals within their scope; the action and reason are retained. |
| T45 | TB2 | R | **Loss of responsibility during incident handover.** Concurrent or repeated handover leaves an incident without an owner or resets its SLA and completed steps. | high (2×3) | SR-34, SR-10 | None with recipient confirmation, version checks and SLA preservation. |
| T46 | TB5 | E | **Production commands and notifications from diagnostic replay.** A historical replay acquires production permissions and reopens a door, invokes a webhook or creates an incident in production. | high (2×3) | SR-35, SR-29, SR-10 | None with ACL isolation and no external adapters. |
| T47 | TB6 | E | **Commands and scope leakage through model responses.** An instruction embedded in a source or a provider response attempts to execute a command or retrieve another user's incident. | high (2×3) | SR-36, SR-05, SR-10 | Model text may be incorrect; output is marked as a suggestion and never executed as an action. |
| T48 | TB2 | T | **Production effects of exercises and expired leases.** A training command reaches a real device, or replaying a stale request restores a previous lock. | high (2×3) | SR-37, SR-29, SR-10 | Presence is advisory; production changes independently verify authorization and aggregate version. |
| T49 | TB2 | E | **Session and scope mixing during browser recovery.** A forged callback, concurrent refresh or late response from an old session retains another session's rows and opens a restricted module. | high (2×3) | SR-38, SR-02, SR-05 | XSS gains the current browser's privileges; server authorization remains mandatory regardless of navigation. |
| T05 | TB1 | I | **Eavesdropping on the event stream.** An attacker reads access-control events containing personal data on the site's network. | medium (2×2) | SR-08, SR-21 | None with correct TLS configuration. |
| T09 | TB1 | T | **Replay of old events.** Previously recorded events are resent to create false incidents. | medium (2×2) | SR-30 | Replays with new identifiers and sequence numbers cannot be distinguished from new events (see T02). |
| T18 | TB2 | D | **API and realtime channel overload.** Excessive requests, expensive queries or numerous WebSocket connections overload the service. | medium (2×2) | SR-17 | Protection against a distributed attack from user networks is limited by the load balancer's capacity. |
| T19 | TB2 | I | **Excessive data disclosure.** API responses or errors disclose data outside the access scope or internal details. | medium (2×2) | SR-05, SR-21 | Return RFC 9457 errors without stack traces or internal addresses. |
| T24 | TB3 | I | **Service access to unrelated data.** A compromised service reads topics and database schemas it does not need. | medium (2×2) | SR-19 | Services with broad read access (Projection Service, API Gateway) legitimately see most data. |
| T32 | TB5 | R | **Repudiation of configuration changes.** An administrator denies changing rules, scenarios or permissions. | medium (2×2) | SR-10 | None. |
| T36 | TB6 | I | **Data leakage in notifications.** Emails and webhooks contain personal data and incident details. | medium (2×2) | SR-21, SR-20 | Content of administrator-defined templates. |
| T38 | TB6 | D | **Keycloak unavailability.** An IdP failure blocks sign-in and token refresh during an incident. | medium (2×2) | SR-03, SR-27 | New sign-ins remain unavailable until Keycloak recovers; existing sessions work until their refresh tokens expire. |
| T08 | TB1 | E | **Connector control of another connector's devices.** A connector attempts to initiate commands or receive commands for another connector's devices. | medium (1×3) | SR-06 | None; the protocol accepts no commands initiated by connectors, and delivery targets only the session serving the device. |
| T15 | TB2 | T | **Cross-site request forgery.** A third-party site sends a request on behalf of an authenticated user. | medium (1×3) | SR-15 | None; the browser does not send the token automatically. |
| T26 | TB4 | I | **Data theft from storage media and backups.** Storage node disks or backup files are copied. | medium (1×3) | SR-22, SR-09 | If the customer declines disk encryption, data on the media remains in plaintext. |
| T29 | TB4 | T | **Schema tampering in Schema Registry.** Registering an incompatible schema breaks consumers. | medium (1×3) | SR-28 | None when write access is restricted. |
| T33 | TB5 | E | **Abuse of administration tools.** DLQ reprocessing or event replay creates false incidents while bypassing authorization. | medium (1×3) | SR-29, SR-10 | None. |
| T37 | TB6 | S | **Token forgery through the IdP.** The platform accepts substituted JWKS or tokens with alg none or an incorrect issuer. | medium (1×3) | SR-03, SR-08 | Compromise of Keycloak itself is outside the platform's scope. |
| T40 | TB7 | T | **Release artifact substitution at the customer site.** Modified images or packages are installed instead of released artifacts. | medium (1×3) | SR-24 | None when signatures are verified. |
| T41 | TB7 | E | **Container escape and excessive process privileges.** A service vulnerability grants root access on the node. | medium (1×3) | SR-24, SR-25 | Address OS kernel vulnerabilities through distribution updates. |
| T04 | TB1 | R | **Integrator repudiation of sent events and command results.** A dispute arises over whether a connector sent an event or a command result. | low (1×2) | SR-10 | Individual event signatures by connectors are not included in the MVP. |
| T10 | TB1 | I | **Reading or modifying the SDK buffer on a connector node.** A local user on the connector node reads or modifies the buffer file. | low (1×2) | SR-09 | The integrator is responsible for protecting the connector node; the SDK creates files accessible only to its own user. |
| T21 | TB2 | T | **Malicious links in incident attachments.** An incident receives a link to a phishing site or a URL using the javascript scheme. | low (1×2) | SR-14 | An external URL with an allowed scheme may still point to a malicious site. |

## 4. Security requirements

| ID | Area | Requirement | Components | Verification | Threats covered |
|---|---|---|---|---|---|
| SR-01 | AUTHN | The connector authenticates only with an mTLS client certificate issued by the deployment operator's or organization's CA. Its fingerprint is bound to the connector registration in the catalog. Revocation immediately closes active sessions. Certificate lifetime is at most 1 year; rotation uses an overlap period (C1, C2). | libs/platform/security, services/connector-gateway, services/resource-catalog | test | T01, T02, T03 |
| SR-02 | AUTHN | Users sign in only through Keycloak (OIDC Authorization Code + PKCE). Enforce a password policy with at least 12 characters and lockout against guessing. MFA is mandatory for admin and supervisor roles. The session idle timeout is 30 minutes and maximum duration is 12 hours (configurable). | deploy/local, libs/platform/authz, web | config | T11, T17, T49 |
| SR-03 | AUTHN | Token validation uses a fixed issuer, checks audience, permits only RS256 and ES256, and checks exp and nbf with at most 30 seconds of clock tolerance. JWKS is fetched only from the configured address over TLS and cached with key rotation. Access token lifetime is 5 minutes; refresh tokens rotate. | libs/platform/security, services/api-gateway | test | T12, T37, T38 |
| SR-04 | AUTHN | Service calls require mTLS with an internal CA and a service token (client credentials) or delegated user token. Each service independently validates the token and permissions. | libs/platform/security | test | T22 |
| SR-15 | AUTHN | Store browser tokens only in memory, never in localStorage or sessionStorage, and never include them in URLs. Authenticate the realtime channel with its first frame. Allow CORS only for approved origins. Send tokens in the Authorization header to prevent CSRF. | services/api-gateway, web | test | T12, T15 |
| SR-38 | AUTHN | The shell validates PKCE state, nonce and the configured issuer's ID token, stores tokens only in memory, coalesces concurrent refreshes and clears data on logout. Role-specific navigation does not replace server authorization. When scope changes, acquiring a new cursor clears the previous snapshot before reading a new one. | web | test | T49 |
| SR-05 | AUTHZ | Deny by default. Apply RBAC + ABAC in API Gateway and again in the owning service. Filter lists, search, realtime streams, reports and exports by access scope. Every denial produces an audit record (ADR-020). | libs/platform/authz, services/api-gateway | test | T13, T14, T19, T47, T49 |
| SR-06 | AUTHZ | Commands require checks of device capabilities and the requester's access scope. Critical commands execute only after approval by a different shift supervisor (CM3). The connector protocol does not allow connectors to initiate commands. The gateway delivers a command only to the session of the connector serving that device. | services/command-service, services/connector-gateway | test | T08, T20, T42 |
| SR-07 | AUTHZ | Response configuration changes (activating rule sets, publishing mappings and scenarios, escalation policies and archiving rules) require the admin role and an audit record describing the change. Disabling or archiving rules for incident types with default priority P1 or P2 immediately notifies shift supervisors. | services/audit-service, services/correlation-engine, services/response-engine, web | test | T31, T35 |
| SR-29 | AUTHZ | Administration tools (psimctl) authenticate as users through OIDC device flow and follow the same authorization model. No service account bypasses permissions. The emergency Keycloak account requires MFA; its use is audited and notifies administrators. | libs/platform/authz, tools/psimctl | review | T33, T46, T48 |
| SR-33 | AUTHZ | Maintenance is available to administrators within their scope and requires an end time, reason, owner and explicit policy. Fire events and P1 are never suppressed. History is retained; open incidents and SLAs remain unchanged. Automatic completion survives restarts and is audited. | services/correlation-engine, services/resource-catalog, web | test | T44 |
| SR-34 | AUTHZ | Incident handover requires recipient confirmation and checks of role, scope and version. The previous operator remains responsible until confirmation. Retries are idempotent; SLA and response scenario are preserved. Only a shift supervisor within the relevant scope may assign duty or force reassignment. Audit every action. | services/incident-service, services/notification-service, web | test | T45 |
| SR-35 | AUTHZ | Diagnostic replay is restricted to the requester's scope and isolated through separate ACLs, topics, consumer groups and storage. Its executor cannot write production signals, incidents, commands or notifications, or call HTTP, SMTP or devices. Audit every run. Projection recovery and production DLQ processing are separate authorized operations. | services/correlation-engine, services/response-engine, tools/psimctl | test | T46 |
| SR-36 | AUTHZ | The assistant receives only server-provided facts within the user's current scope, without bearer tokens or tools. An administrator configures the endpoint and model. Enforce response time and size limits and server-provided source links. Recheck permissions and source version after the response; failure preserves the original authorized facts. | services/api-gateway, services/incident-service | test | T47 |
| SR-37 | AUTHZ | Exercises have separate state, owner, version and expiry. Training actions cannot create production commands, incidents or notifications. Presence grants no business permissions; leases are bounded by time and generation identifier. Alarm suppression requires shift supervisor permission, a reason and an end time. | services/api-gateway, services/incident-service | test | T48 |
| SR-08 | CRYPTO | Require TLS 1.3 at external boundaries (TB1, TB2), and at least TLS 1.2 with AEAD suites inside the platform. Certificate verification is always enabled. No exposed port permits plaintext traffic. Enable HSTS for the web interface. | deploy, libs/platform/security | scan | T03, T05, T30, T37 |
| SR-09 | SECRETS | Secrets must not appear in plaintext configuration or environment variables accessible to other processes. Use files owned by the service user with mode 0400, or a secret store. Exclude secrets from logs, traces and diagnostic bundles. Store CA private keys and audit checkpoint signing keys separately with restricted access. Document rotation procedures. An automated test starts services with known test secrets, then verifies that their values are absent from logs, traces and the diagnostic bundle. | deploy, libs/platform/runtime, libs/platform/security, tools/psimctl | test | T10, T26, T34 |
| SR-10 | AUDIT | Audit all user actions, access denials, configuration changes, connector session events (opening, rejection and revocation) and command lifecycle events. Maintain a hash chain and signed checkpoints outside the database. Database permissions prohibit UPDATE, DELETE and TRUNCATE (ADR-021). | services/api-gateway, services/audit-service, services/connector-gateway | test | T01, T04, T14, T17, T20, T25, T31, T32, T33, T43, T44, T45, T46, T47, T48 |
| SR-11 | INPUT | Validate all incoming messages with protovalidate at the boundary (Connector Gateway and API Gateway). Limit gRPC messages to 4 MB, event batches to 1 MB and 1000 events, and REST bodies to 1 MB (imports up to 100 MB, floor plans up to 20 MB). Reject unknown REST fields. | services/api-gateway, services/connector-gateway | test | T07, T16, T42 |
| SR-12 | INPUT | Fuzz all parsers of untrusted data, including connector protocol decoding, CSV and JSON imports, rule and mapping expressions, and realtime frames. CI requires 10 crash-free minutes for the rule parser and validator. Release qualification requires a complete 24-hour run of every target. | services/connector-gateway, services/correlation-engine, tools/security | test | T07, T27 |
| SR-13 | INPUT | Access PostgreSQL and ClickHouse only through parameterized queries. Concatenating user input into query text is prohibited and checked by static analysis in CI. | libs/platform/clickhouse, libs/platform/pg, services/normalizer, tools/cpp | scan | T27 |
| SR-14 | INPUT | Render user and connector data as text in the web client, without inserting HTML. Use a strict CSP without inline scripts, X-Content-Type-Options, frame-ancestors 'none', and Referrer-Policy. Incident links allow only http and https with rel="noopener noreferrer". | services/api-gateway, web | test | T12, T16, T21 |
| SR-30 | INPUT | Prevent replay through event_id and source sequence deduplication, command deadlines and idempotent command_id, signed webhooks with a timestamp and a 5-minute acceptance window, and expiring realtime resume tokens bound to the user. | services/api-gateway, services/command-service, services/normalizer, services/notification-service | test | T02, T09 |
| SR-31 | INPUT | Accept floor plans only as PNG/JPEG up to 20 MB, checking file signatures, decoding and pixel limits. The platform generates object names; the bucket is private. Reads and writes enforce RBAC and access scope. Independent backups include versions and SHA-256. | deploy, services/resource-catalog, tools/psimctl, web | test | T42 |
| SR-16 | DOS | Protect ingestion with credits, per-connector event rate limits, batch and message limits, one active session per connector and a connection limit per gateway. On SDK buffer overflow, evict info events first and emit system.connector.buffer_overflow. Never evict alarm or fault events. | libs/platform/connector, services/connector-gateway | test | T02, T06 |
| SR-17 | DOS | Protect the API and realtime channel with per-user and per-endpoint rate limits, a maximum page size of 1000, request deadlines, at most 5 WebSocket connections per user, disconnection of slow clients and export size limits. | services/api-gateway | test | T18 |
| SR-18 | DOS | Prevent resource exhaustion through bounded queues and backpressure, Kafka client quotas, disk utilization monitoring for Kafka, PostgreSQL and ClickHouse with alerts at 70% and 85%, and enforced retention. | deploy/observability, libs/platform/async, libs/platform/kafka | test | T06, T28 |
| SR-27 | DOS | Protect alarm response availability with alerts on pipeline lag and event -> console latency SLO violations. The console indicates loss of connectivity. Per-connector limits prevent a source from displacing others. Runbooks describe degraded operation. | deploy/observability, libs/platform/connector, tools/bench | test | T02, T06, T28, T38 |
| SR-19 | NET | Storage and internal ports are reachable only within the platform network. The installer provides a firewall rule template. Kafka, PostgreSQL, Redis-compatible storage and ClickHouse require authentication and TLS. Generate service-specific Kafka ACLs from the topic registry, allowing writes only to the service's own topics. Use separate PostgreSQL roles and ClickHouse users per service with minimum permissions. | deploy, libs/platform/clickhouse, libs/platform/kafka, libs/platform/pg, libs/platform/redis | config | T22, T23, T24, T30 |
| SR-20 | INTEGR | Outbound calls (webhooks, automatic steps and SMTP) may target only addresses in the administrator's allowlist. Reject private, loopback, link-local and metadata addresses by default, checking after DNS resolution. Verify TLS, sign webhooks with HMAC-SHA256, and enforce timeouts and response size limits. | services/notification-service, services/response-engine | test | T35, T36 |
| SR-28 | INTEGR | Only the installer and CI may write schemas to Schema Registry. Services have read-only access. Configure compatibility rules for every subject. | libs/platform/kafka, tools/deploy | config | T29 |
| SR-21 | DATA | Minimize personal data. By default, replace access-control card identifiers with a keyed HMAC during normalization. Exclude personal data from logs, apply RetentionSettings, use notification templates without personal data and audit data exports. | services/audit-service, services/normalizer, services/notification-service | test | T05, T19, T36 |
| SR-22 | DATA | Recommend full-disk encryption on storage nodes; the installer checks it and warns when absent. Encrypt PostgreSQL, ClickHouse and configuration backups. | deploy, tools/psimctl | config | T26 |
| SR-32 | DATA | Device state is separate from connectivity and actual/stale/unknown quality. Loss of connectivity requires fresh confirmation; old events and snapshots cannot improve freshness. Reconciliation considers source sequence and epoch. Filter snapshots and deltas by access scope. | libs/platform/connector, services/api-gateway, services/connector-gateway, services/normalizer, services/projection-service | test | T43 |
| SR-23 | SUPPLY | Pin dependencies in lockfiles (Conan, pnpm and buf.lock), and base and tooling images by digest. Produce a CycloneDX SBOM. CI checks licenses and vulnerabilities, blocking critical and high findings. Builds must be reproducible. | tools/release, tools/toolchain | scan | T39 |
| SR-24 | SUPPLY | Sign release artifacts (images and packages). The installer verifies signatures before installation and upgrades. Run containers without root, with a read-only filesystem where possible and without unnecessary capabilities. | tools/deploy, tools/release, tools/security | test | T40, T41 |
| SR-25 | OPS | Harden the deployment with a minimal OS package set, dedicated service users and systemd restrictions (NoNewPrivileges, ProtectSystem and PrivateTmp). Do not use privileged containers. The Keycloak administration console must be inaccessible from user networks. | deploy, tools/security | pentest | T41 |
| SR-26 | OPS | Security monitoring alerts on spikes in failed logins, connector rejections and access denials, disabled rules, failed periodic audit integrity checks and certificates expiring within 30 days. | deploy/observability, services/audit-service | test | T01, T11, T20, T25, T31 |

## 5. Requirements by component

Component changes require verification of their associated security requirements.

| Component | Requirements |
|---|---|
| deploy | SR-08, SR-09, SR-19, SR-22, SR-25, SR-31 |
| deploy/local | SR-02 |
| deploy/observability | SR-18, SR-26, SR-27 |
| libs/platform/async | SR-18 |
| libs/platform/authz | SR-02, SR-05, SR-29 |
| libs/platform/clickhouse | SR-13, SR-19 |
| libs/platform/connector | SR-16, SR-27, SR-32 |
| libs/platform/kafka | SR-18, SR-19, SR-28 |
| libs/platform/pg | SR-13, SR-19 |
| libs/platform/redis | SR-19 |
| libs/platform/runtime | SR-09 |
| libs/platform/security | SR-01, SR-03, SR-04, SR-08, SR-09 |
| services/api-gateway | SR-03, SR-05, SR-10, SR-11, SR-14, SR-15, SR-17, SR-30, SR-32, SR-36, SR-37 |
| services/audit-service | SR-07, SR-10, SR-21, SR-26 |
| services/command-service | SR-06, SR-30 |
| services/connector-gateway | SR-01, SR-06, SR-10, SR-11, SR-12, SR-16, SR-32 |
| services/correlation-engine | SR-07, SR-12, SR-33, SR-35 |
| services/incident-service | SR-34, SR-36, SR-37 |
| services/normalizer | SR-13, SR-21, SR-30, SR-32 |
| services/notification-service | SR-20, SR-21, SR-30, SR-34 |
| services/projection-service | SR-32 |
| services/resource-catalog | SR-01, SR-31, SR-33 |
| services/response-engine | SR-07, SR-20, SR-35 |
| tools/bench | SR-27 |
| tools/cpp | SR-13 |
| tools/deploy | SR-24, SR-28 |
| tools/psimctl | SR-09, SR-22, SR-29, SR-31, SR-35 |
| tools/release | SR-23, SR-24 |
| tools/security | SR-12, SR-24, SR-25 |
| tools/toolchain | SR-23 |
| web | SR-02, SR-07, SR-14, SR-15, SR-31, SR-33, SR-34, SR-38 |
