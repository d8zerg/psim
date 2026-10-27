# ADR-060. Realtime resume from Kafka and operational snapshots

<!-- language-links -->
[English](0060-realtime-delivery.md) | [Русский](../ru/adr/0060-realtime-delivery.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-019](0019-realtime-channel.md), [ADR-059](0059-console-projections.md)

## Solution

Each Gateway reads incident events and responses through manually assigned read_committed Kafka, with all partitions handled individually. Consumers do not join groups or share events with other instances. Snapshots from the Projection Service include offsets for both topics. The recovery token is not tied to any process or Gateway node.

A limited log in memory speeds up short interruptions. When no desired range is available, the Gateway reads from Kafka until the captured end of a partition. Data is filtered by verified tenant, access scope, subscription, and agreed types. Missing transaction markers are treated as positions. Recovery is limited to 15 minutes, based on retention, message count, bytes, and reading duration. Incomplete ranges require a new snapshot.

The connection supports multiple independent subscriptions. An empty capabilities list indicates the previous `incident_event`; new types are explicitly added. The log records each appropriate incident event or supported response change. Terminal changes are also sent to the active feed so that clients can remove closed incidents. Requests for commands, notifications, and private alerts do not flow into the main response stream.

`STREAM_LOCATION_STATUS` sends a complete current image of the selected object from the Projection Service. It includes devices, regions, connectors, quality, maintenance and duty assignments. The image replaces the previous one; intermediate snapshots can be coalesced. This is the current state, not a device history log. When recovering, a fresh image is sent, so an old acknowledgment does not restore online or actual status.

The Gateway does not calculate or store individual device models. It requests from the Projection Service with an original JWT, coalescing requests for one site only when tenant, roles and access scopes match. Images are checked every 50 ms; this also detects observation freshness and lease expiry when no new Kafka events occur. Each group uses the current connection token. Changes to rights or capabilities cancel previous subscriptions and clear the queue.

Queues, individual messages, subscriptions, historical reads, and connections are limited. Slow recipients are disconnected; each write has its own deadline. An `REALTIME_SLOW_CONSUMER` error is sent if transport is still accepting data. The client resumes reading from the last received token. Expiration of the JWT closes the connection; updates are made through a repeated Authenticate message. `permessage-deflate` negotiation is supported without context preservation between messages.

## Verification

Two Gateways are tested, reconnection to another node, restart, site, region, and incident filters, multiple subscriptions, old client, type matching, current operational snapshot, slow recipient, and memory limits. A load scenario measures delivery for 500 simultaneous WSS clients. The method records event count, rate, data size, and connection distribution across nodes; the result does not replace full platform load testing.
