# Real-time delivery

<!-- language-links -->
[English](realtime.md) | [Русский](../ru/engineering/realtime.md)
<!-- /language-links -->

The API Gateway serves `wss://<host>/v1/realtime`. Solutions and compatibility are described in [ADR-060](../adr/0060-realtime-delivery.md), while the original protocol is in [ADR-019](../adr/0019-realtime-channel.md). Snapshots are provided by [Projection Service](console-projections.md).

## Connection and subscriptions

The client receives a REST snapshot, opens a WebSocket with the allowed Origin, and sends an Authenticate with a JWT. `supported_change_types` defines understood types: `incident_event`, `response_event`, `location_status`. An empty list retains the previous `incident_event`. Authentication returns the intersection of supported types and available streams. The general TypeScript client provides `supportedChangeTypes` and handler `onCapabilities`.

`STREAM_INCIDENTS` sends stream events, while `STREAM_INCIDENT` requires `filter.incident_id`. Filters `site_ids` and `zone_ids` intersect with verified JWT scopes; they do not expand access. Tenants are taken from JWT. Closing an incident is treated as a normal change, so the client removes it from the active stream. Aggregate versions protect against duplicates.

`response_event` includes changes to scenarios, SLAs, escalation rules, policies, absence of plans, and duty. For events without a site, the Gateway checks incidents through Incident Service with the original JWT. Inability to check requires a new snapshot. Private command and notification delivery intents are excluded. Roles are additionally checked for reading scenarios or duty.

Connection supports independent subscriptions with unique `subscription_id`. Unsubscribe terminates the subscription. Re-authentication updates the JWT. Changing tenants, roles, scopes, or types clears the queue and cancels previous subscriptions with ResyncRequired. Expired JWT closes the connection; the server does not send data after the expiration time. The first authentication takes 5 seconds.

## Restoration

The token is opaque to the client. Version 2 contains the tenant, issuance time, and vectors of all partitions `psim.incidents.events.v1` and `psim.response.events.v1`. It does not depend on the Gateway node. The client stores the token only after successful Delta application and sends the last token upon recovery. The TypeScript client closes the connection when the synchronous handler throws and rereads from the previous token; applications must respect aggregate versions.

Each Gateway reads all partitions readcommitted, without splitting by consumer group. Partition assignments are fixed at startup; changing their number requires restarting the Projection Service and all Gateways as part of topology changes. The memory log has a limit of 10,000 messages, 8 MiB, and 15 minutes. If no range is available, Kafka reading is performed until the captured ends, then the live log is caught up. Identical vectors share one historical read. Up to 4 different reads are allowed simultaneously; completed results can be reused within 2 seconds.

Historical reading is limited to 10,000 original messages, 4 MiB, and 5 seconds after receipt of boundaries. The vector must exactly cover current partitions; positions outside retention, from the future, tokens older than 15 minutes, old events, or exceeding budget require ResyncRequired. Incomplete ranges are not reported as successful recovery. After ResyncRequired, the client receives a new consistent REST snapshot. The old token of the previous implementation requires one new snapshot upon update.

## Operational status

`STREAM_LOCATION_STATUS` requires `location_status`, `filter.location_id`, and the token of the corresponding REST snapshot. After subscription, a fresh full `GetLocationStatusResponse` is sent; each subsequent image replaces the previous one completely, including deleted resources. This is the actual state of devices, zones, and connectors, as well as quality, maintenance, and duty. Intermediate images can be combined; the stream is not the history of each device change.

The Gateway requests Projection Service with the original JWT. Requests to one object are combined only when tenants, roles, and scopes are the same. The interval between passes is 50 ms; request time is added to the interval. Expiration of freshness, maintenance, or lease is detected without new Kafka events. During recovery, the old image does not confirm online or actual. The Gateway does not build a separate model of devices. The number of different objects and sets of rights affects the load on the Projection Service.

## Limitations and observability

| Parameter `settings` | Default |
|---|---:|
| `realtime_max_connections` | 1024 TCP connections, including REST and handshake |
| `realtime_max_subscriptions` | 16 per connection |
| `realtime_queue_messages` | 1000 messages per connection |
| `realtime_queue_bytes` | 4194304 bytes per connection |
| `realtime_total_queue_bytes` | 67108864 bytes per process, including in-flight writes |

The socket write deadline is 2 seconds. When the limit is exceeded, `REALTIME_SLOW_CONSUMER` is sent; when writing is still possible, then the connection is closed. If writing is blocked, the connection closes after the time limit. Stopping Kafka reading or processing operational images removes readiness, closes existing WebSocket, and rejects new connections until the process is restarted. Control frames are limited to 16 KiB and 100 frames/second with burst of 200. `permessage-deflate` is supported without saving context between messages.

`psim_realtime_connections` shows active WebSocket connections, `psim_realtime_slow_consumers_total` shows queue overflow or socket write timeout. `psim_realtime_delivery_seconds` measures Kafka CreateTime -> placing a live change in the queue; historical recovery and rendering time are not included. The API Gateway panel shows connections, delays, and disconnections of slow clients.

## Verification

```bash
task realtime:test
task realtime:bench
task realtime:sanitizers
task realtime:coverage
```

The test uses two Gateways, real WSS/JWT, Kafka, PostgreSQL, Valkey, and service owners. Each Gateway in the test has a memory limit of 1 GiB; for the overall address of the client load `ip_rate_per_second` and `ip_rate_burst`, it is set to 10000. Each node receives 250 connections, with 20 changes every 200 ms: 10000 deliveries without losses or repetitions. The delay is measured from `produced_at`, set by the native publisher directly before serialization and sending to Kafka, to when the client processes the message. Process preparation and schema generation are completed before the time limit; containers share the host clock. Then the second node stops, 5 incident events are published, and the duty is changed, with all 500 clients restarting on the rebuilt node from Kafka: 3000 deliveries from both topics without a new snapshot.

A typical incident has a short header `Realtime verification`; testing the slow receiver uses 64 messages with a header of 160000 ASCII characters. These data are created in a separate test Compose project.

Separately, filters, multiple subscriptions, an old client, private variants, authorization through the owner, live image, and lease expiration, role changes, invalid positions, JWT, and the receiver that stopped reading are tested. Regular builds require p99 not exceeding 200 ms; tooled builds test correctness. The local profile tests the channel; full platform load of LOAD-50K is a separate test.

Artifacts: `build/realtime/<preset>/result.json`, logs from both Gateways, and `peer-result.json`. Coverage additionally includes model console checks, Catalog, reporting, the browser end-to-end baseline, and module tests.

The validity of the bearer token is tested even during idle connections. A user’s revocation, introspection error, or role/access scope changes closes the connection; reconnection gets a fresh snapshot. The cache result does not exceed 500 ms, and the delivery authorization freshness bound is one second. Details of configuration and revocation limits: [Authorization](../security/authorization.md).
