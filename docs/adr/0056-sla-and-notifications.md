# ADR-056. SLA, duty assignment, and Notification Delivery

<!-- language-links -->
[English](0056-sla-and-notifications.md) | [Русский](../ru/adr/0056-sla-and-notifications.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-008, ADR-012, ADR-042, ADR-043

## Context

The Response Engine already stores notification intents and explicit escalations. To respond within the SLA, separate acceptance and resolution deadlines are required, along with resolution of the current duty assignee and durable delivery through channels. The change of the responsible person should not change the deadlines. An SMTP acknowledgment means the server has received the email; losing the connection after DATA prevents confirmation of delivery.

## Solution

1. The Response Engine holds SLA policies, duty assignments, and `escalation` schema tables. The Notification Service holds contacts, delivery queue, receipts, and `notification` schema. The general read-only adapter for identity catalog is in `libs/platform/identity`; it is reused by the Incident Service.
2. The Incident Service records the policy identifier along with the incident type version. When a snapshot is received for the first time, the Response Engine records the full policy version and original priority. The acceptance deadline is from `created_at`, and the resolution deadline is from `acknowledged_at`. Assignment, responsibility transfer, priority change, and policy editing do not change existing deadlines. Reopening uses existing SLA.
3. Each SLA has its own violation and escalation levels. The action identity is `(tenant, incident, sla_kind, level)`. Checking the current snapshot, timer execution, notification request, escalation event, and audit are all recorded in one transaction with the incident locked. Kafka-partition does not define timer ownership. Duplicates and old snapshots do not change deadlines.
4. The recipient `current_duty` is resolved when an SLA notification request is created. Assignment is checked by active account, role, and site area. If no active duty assignment exists, a backup user is used, and an audit is published `response.duty.missing`. If all settings are missing, the request remains in the timer until configuration is corrected.
5. The identifier of a single notification is derived from the request identifier, user, and channel. Inbox compares the content of the repeated request. Delivery channels use saved destinations and unchanged bodies; status, attempts, and outbox are stored in PostgreSQL. Changing status publishes `NotificationUpdated` and audit. Incident Service adds status and receipt to the timeline.
6. In-app delivery is a transaction record, accessible only to the recipient in the current access scope. For email, SMTP is used over TLS with name and certificate chain verification, stable Message-ID, size limit, and general timeout. A successful SMTP response gives `SENT`: this is server acceptance, not confirmation of reading or delivery to the inbox.
7. SMTP does not support atomic receipt storage with our DB. Before external calls, `UNKNOWN` is saved; a specified temporary failure returns the record to the queue. An unknown outcome after DATA or process stoppage is not automatically repeated. The administrator checks Message-ID against the SMTP server log and saves verified results through API. Losing the last SMTP response does not trigger automatic duplication. Fully automatic exactly-once delivery cannot be guaranteed with ordinary SMTP.
8. Webhook uses HTTPS, HMAC-SHA256 signing of identifier, body and timestamp with a five-minute reception window, `Idempotency-Key`, fixed list of full URLs, size limit, and timeout. After DNS, all addresses are checked; connection uses only verified results. Non-public, local, service, and transition addresses are rejected. Redirects are not performed. Special ranges are checked against [IANA IPv4](https://www.iana.org/assignments/iana-ipv4-special-registry/) and [IANA IPv6](https://www.iana.org/assignments/iana-ipv6-special-registry/) registries; neighboring public networks remain accessible. The receiver must atomically save the identifier and effect, and return the same receipt if repeated. After a crash, the service checks the original receipt with the same idempotent request; if the result is unknown, it saves `UNKNOWN` for confirmation by the administrator. Such checking does not create a new logical notification. Without receiver-side deduplication, remote exactly-once delivery cannot be guaranteed.
9. The number of attempts is limited; repeated temporary failures use increasing delay. Permanent failure or exhaustion with a specific failure gives `FAILED` and audit. If the recipient remains unknown after the last attempt, it saves `UNKNOWN` for comparison with the receiver. Frequency is limited by tenant, user, site, and channel. After the notification threshold, they are combined into a durable summary; original identifiers are saved, and the body of the sent summary is not changed.
10. Local exception for one URL in a private network is allowed only in explicit development mode. It is used by isolated TLS receiver for checks. The working configuration does not include this exception.

## Consequences

Deadlines continue to be calculated during process stops; timer accuracy up to one second is related to the accessible environment. Delay in event receipt and PostgreSQL availability may delay detection of violations. Temporary channel failures are automatically recovered within the budget of attempts. An unknown SMTP delivery outcome requires verification, not guesswork about the result. Contacts and active backup recipients on the site are configured before policy implementation.

The SMTP server in MVP is a trusted relay, accessible via verified TLS and limited network route. SMTP AUTH and DSN are not implemented. The webhook receipt confirms agreement with the receiver; signature does not replace TLS and deduplication on the receiving side.

## Verification

[Guide](../engineering/notifications.md) contains tasks and artifact locations. Actual verifications include two instances of each service, duty changes between escalation levels, backup recipients, process stops, channel disconnections, receipt loss after reception, Kafka re-delivery, frequency suppression, signature, prohibition of local webhook, and isolation of user notifications.
