# ADR-019. Realtime channel: WebSocket, snapshots, deltas and offset-based resumption

<!-- language-links -->
[English](0019-realtime-channel.md) | [Русский](../ru/adr/0019-realtime-channel.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-008, ADR-020, [crosscutting.md](../architecture/crosscutting.md#9-realtime-snapshot-and-deltas)

## Context

The console must display a new incident within 1 s of its event at p99, support 500 concurrent consoles, recover from disconnection without missing changes (S6, S11), and display only data within the user's access scope.

## Options considered

### Transport

| Option | Advantages | Disadvantages |
|---|---|---|
| Periodic REST polling | Simple | Latency and load depend on polling frequency |
| Server-Sent Events | Simple, uses HTTP | One-way: subscriptions and filters require separate requests; browser HTTP/1.1 connection limits |
| gRPC-web | Typed | Streams only server -> client; requires a proxy |
| **WebSocket** | Bidirectional subscriptions, filters and acknowledgements in one connection; widely supported | Requires an application protocol |

### Delta source

| Option | Advantages | Disadvantages |
|---|---|---|
| Projection Service pushes deltas after projection updates | Client sees the projection's data | Projection processing contributes to console latency; complex resumption |
| **Deltas directly from domain-event topics; projection snapshots with consistency offsets** | Projections are outside the latency path; Kafka offsets enable resumption without a separate log | Every API Gateway instance reads entire incident-event topics, whose volume is small |

## Decision

1. The client receives a `GET /v1/feed/incidents` snapshot with the `{topic, partition -> offset}` vector at which it is consistent.
2. The client opens `wss://.../v1/realtime`, authenticates with a token and subscribes using `subscribe {stream: incidents, filter, resume_token}`. The resume token encodes the offset vector.
3. API Gateway reads all partitions of `psim.incidents.events.v1` and `psim.response.events.v1` without a consumer group, filters by access scope (ADR-020) and subscription filters, and sends deltas with the current token.
4. The client applies a delta only when the aggregate `version` exceeds the known version.
5. After disconnection, the client reconnects with its latest token and receives missed changes. If offsets are outside retention or the interruption exceeds `max_resume_gap`, 15 min by default, the server returns `REALTIME_RESYNC_REQUIRED` and the client obtains a new snapshot.
6. For a slow client, the send queue is bounded to 1,000 messages or 4 MB by default. Overflow closes the connection with `REALTIME_SLOW_CONSUMER`; recovery follows rule 5.
7. Messages use JSON according to realtime-protocol contract schemas, with `permessage-deflate` compression.
8. After refreshing its access token, the client sends `Authenticate` on the same connection. An expired token closes the connection.

## Consequences

- Projection Service must return the offset vector with each snapshot.
- API Gateway scales horizontally with connection count; each instance receives the shared event stream.

## Verification

Load test with 500 consoles; a 5 min disconnection test with no missed changes.

## Implementation refinement

[ADR-060](0060-realtime-delivery.md) specifies a node-independent version 2 token, historical-read limits, capability negotiation, exclusion of private response variants and current images from Projection Service. Reauthentication uses Authenticate. An unavailable range requires a new snapshot; equipment-state changes carry the complete current image, and intermediate images may be coalesced.
