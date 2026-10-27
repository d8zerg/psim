# SLA, Shifts, and Notifications

<!-- language-links -->
[English](notifications.md) | [Русский](../ru/engineering/notifications.md)
<!-- /language-links -->

The Response Engine manages SLA policies and shifts, while the Notification Service delivers notifications. Data is stored in PostgreSQL; integration uses inbox/outbox and `psim.response.events.v1`. Solutions and guarantees are defined in [ADR-056](../adr/0056-sla-and-notifications.md).

## Policy and Shift Configuration

The administrator creates a policy through `POST /v1/escalation-policies` and specifies it in `policyId` and `IncidentType.escalationPolicyId`. The policy includes SLA for used priorities and sequential levels with increasing delay after a breach. The acknowledgement deadline is the creation of an incident plus `acknowledgeWithin`; resolution is due at acknowledgement plus `resolveWithin`. The policy and initial priority are captured for each incident. Changing the shift, responsible person, or policy does not affect deadlines.

`GET /v1/escalation-policies` returns policies with pagination. `PUT /v1/escalation-policies/{policy_id}` requires `If-Match`. Changes require `Idempotency-Key` with UUID and administrator rights for the entire tenant. A duplicate key with different content is rejected.

The supervisor assigns a shift through `PUT /v1/sites/{site_id}/duty`. The body contains `siteId`, `userId`, and mandatory `fallbackUserId`. An empty `userId` indicates no shift. To change an existing record, `If-Match` is required; `GET` of the same address returns the record and ETag. Accounts must be active, have the role of operator or supervisor, and have access to the site. The recipient `{"currentDuty": true}` is resolved when the escalation notification is sent. Missing a working shift includes a backup recipient, a warning in the notification text, and an audit `response.duty.missing`.

## Contacts and Channels

`PUT /v1/notification-contacts/{user_id}` stores `userId`, `siteId`, `email`, and `webhookUrl`. To change an existing record, ETag and `Idempotency-Key` are required; access is granted by the site administrator. Roles and recipient access scopes are verified through the identity catalog. Registered contacts on the site are used for role-based sending, with no more than one hundred recipients per request.

Users read their in-app notifications through `GET /v1/me/notifications` with `unreadOnly`, `pageSize`, `pageToken`. `POST /v1/me/notifications/{notification_id}:read` marks reading. Another user cannot access the record; changing the access scope is considered when making another API request. The response contains `unreadCount` for all available collections, regardless of the current page.

SMTP works over TLS with certificate verification and server name verification. The message contains the notification text and incident context in UTF-8 with MIME base64. The status `SENT` indicates that the server has received the message; it does not confirm delivery to the inbox. A definitive transient rejection is retried. If the result is lost after DATA or the process stops, the record remains `UNKNOWN` and is not sent again. The administrator compares the stable Message-ID with the relay log and calls `POST /v1/notifications/{notification_id}:resolve` with `accepted`, `receiptId`, `If-Match`, and `Idempotency-Key`. The API only records the verified outcome and does not send the message again.

The operations engineer finds uncertain sends through a read-only request to PostgreSQL; `tenant_id` is set by the psql variable:

```sql
SELECT id AS notification_id, site, body->>'channel' AS channel,
       body->>'version' AS version, body->>'failureCode' AS failure_code
FROM notification.notifications
WHERE tenant = :'tenant_id'::uuid AND status = 'unknown'
ORDER BY id;
```

For SMTP relay, the log is checked through `Message-ID: <notification_id@psim.invalid>`. The absence of a line in an unfinished log does not confirm a failure. The API accepts only results that have been processed on the receiver.

The webhook receives compact JSON with `notificationId`, recipient, incident, template, data, and identifiers of combined notifications. Headers:

- `Idempotency-Key`: notification identifier.
- `X-PSIM-Timestamp`: Unix time of the current attempt in seconds; reception window is five minutes.
- `X-PSIM-Signature`: `sha256=` and HMAC-SHA256 string `timestamp.notificationId.payload` in hex. `payload` contains exact bytes of the body; separator is a period.

The recipient atomically saves the effect and identifier and returns `{"notificationId":"...","accepted":true,"receiptId":"..."}`. Repeating the same body returns the original receipt. Temporary failure or loss of response causes a retry with the same identifier and body. Permanent HTTP failure and exhaustion of attempts with a specific failure result give `FAILED`. If the receiver could save the effect when attempts were exhausted but no receipt was received, `UNKNOWN` is saved. After a crash during the final attempt, the service checks the receipt with the same idempotent request. If the receipt cannot be obtained, `UNKNOWN` is saved; the administrator records the verified outcome through the API `:resolve`.

After reaching a threshold frequency per user, site, and channel, the original notifications go to `AGGREGATED`. Their `data.summaryId` indicates a durable summary with `aggregatedNotificationIds`. Combined original records are not shown as individual unread notifications. The summary contains the number of messages, original text, and identifier of the first incident through `data`; it is sent at the end of the window; it contains no more than five hundred identifiers. Additions after starting the sending form a separate summary.

## Configuration

General parameters for PostgreSQL, Kafka, TLS API, and JWKS match those of [Response Engine](response-engine.md). Both services use `directory_base`, `directory_client`, `directory_roles_client`, `directory_secret_file`: a read-only client in Keycloak with read permissions for users and roles. Secrets are read from files in mode 0400 and do not go into logs.

The Notification Service additionally uses:

| Parameter | Purpose |
|---|---|
| `smtp_host`, `smtp_port`, `smtp_from` | Trusted SMTP relay with implicit TLS, default port 465 |
| `delivery_ca_file` | CA SMTP and webhook; an empty value uses the system trust store |
| `webhook_urls` | List of fully allowed HTTPS URLs |
| `webhook_secret_file` | HMAC key length of at least 32 bytes |
| `delivery_attempts` | Maximum attempts, default 6 |
| `retry_ms` | Base delay for retries, default 1000 ms |
| `rate_limit`, `rate_window_ms` | Threshold frequency and window, default 20 and 10000 ms |
| `fixture_webhook_url` | One URL for local verification; only allowed with `allow_plaintext=true` |

The production configuration uses HTTPS, verified CA, and a separate HMAC key. URLs with userinfo, fragment, prohibited schemes, and internal network addresses are rejected. DNS is checked when configuring contacts and before connecting; redirection is not performed. SMTP AUTH and automatic DSN are not available in MVP.

## Verifications

All processes, infrastructure, and build tools are run in the native Docker Engine on Linux:

```
task notifications:test
task notifications:sanitizers
task notifications:coverage
```

Artifacts are located in `build/notifications/<preset>/`: JSON reports, process logs, and SQLite test receipts. The test SMTP keeps track of the actual number of deliveries for each Message-ID; the webhook verifies HMAC, integrity of the body, and deduplication. Individual receiver modes simulate unavailability and loss of response after receipt is captured.
