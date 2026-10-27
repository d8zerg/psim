# ADR-048. Connector SDK, disk buffer and state reconciliation

<!-- language-links -->
[English](0048-connector-sdk.md) | [Русский](../ru/adr/0048-connector-sdk.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-004, ADR-005, ADR-006, ADR-017, ADR-042, ADR-043, ADR-047

## Context

The initial minimal simulator implements one session directly through gRPC. Integrators need reconnection, durable event buffering during outages, flow control and safe command handling. Reconnection must not present old history as current device state.

## Decision

1. Place the C++ SDK in `libs/platform/connector`, with public interface `psim/platform/connector/client.hpp` and CMake target `psim::platform_connector`. It depends on contracts and security libraries, but not Kafka, PostgreSQL, services or `platform_stream`. One `GrpcProcess` in `main` initializes OpenSSL before gRPC, owns EventEngine and shuts it down after RPC objects. The SDK follows gRPC's `OPENSSL_INIT_NO_ATEXIT` policy: the global cryptographic context lives for the process lifetime and must not unload concurrently with TLS destructors on detached gRPC threads. Closing releases RPCs, channels, certificates and transport resources; the previous name in `platform_stream` remains an alias.
2. Require TLS 1.3, a client certificate, trust verification and server name verification. Read keys through `security::read_secret`. The public API provides no plaintext mode, name override or disabled certificate verification. Settings and errors do not expose secrets.
3. Persist each event synchronously in the journal before sending. The SDK assigns UUIDv7, an epoch and a monotonically increasing source sequence. Retries preserve identifier, epoch, sequence, timestamps and payload. Accept acknowledgements only for the current batch, with complete and consistent accounting of accepted and permanently rejected events. Keep an event buffered until its acknowledgement is persisted.
4. Journal records use JSON with SHA-256 and a terminating newline. Writes and acknowledgements require `fsync`. Recovery removes only an incomplete tail; corruption of a complete record prevents opening. Compaction uses a separate file, `fsync`, atomic rename and directory `fsync`. Directory creation also synchronizes parent entries. A separate `flock` prevents two owners. Directory permissions are `0700`, files `0600`; final-component symlinks are rejected. Guarantees require a local filesystem with correct `fsync` and atomic `rename`, not network storage.
5. Bound journal size, event count and command registry size. Overflow returns an error to apply source backpressure; old events are not evicted. Compaction may temporarily need a second file up to the journal size limit. A write or synchronization failure requires reopening the journal. Acknowledge external ingestion only after successful `publish`. If local I/O has an unknown outcome, the operator inspects the journal before submitting again.
6. The session uses separate gRPC reading and writing threads, with bounded incoming, control and handler queues. Provide an independent registration/session timer, `BatchAck` timeout, heartbeat, shutdown cancellation and exponential reconnect backoff with jitter. Only one unacknowledged batch is in flight, bounded by gateway settings, credits and the local limit of 32 events / 64 KiB.
7. Persist command start before execution and its result before sending `CommandResult`. A repeated identifier returns the saved result; a different payload with that identifier is rejected. If the process exits between start and result persistence, return an unknown outcome without executing again. Do not execute expired commands. Do not automatically prune the command journal; a full registry blocks new commands. Physical exactly-once actions are not promised; unknown outcomes require device inspection. Handlers must honor deadlines and shutdown signals.
8. Extend v1 compatibly with `Register`/`Registered` capabilities, event observation metadata and `StateReport`. Offer `1.1`, then `1.0`; send new messages only after negotiating `state.v1`. Advertise snapshot polling separately through `snapshot.v1`, only when a handler exists. Advertise commands through `commands.v1`. A 1.0 gateway still accepts events; missing capabilities do not imply state reconciliation support.
9. Capture `(source_ref, source_epoch, source_seq)` before requesting a device snapshot. Assign that exact boundary even if new events arrive during the request. Historical input is not a current observation by default; the integrator explicitly sets `Observation::kCurrent` for a fresh device observation. In a new session, send all old events with `live_observation=false` to extend history. `StateTracker` checks order, current-session membership of live events and quality; an older snapshot cannot replace newer state. Unqueried sources remain `unknown`; device availability is separate from connector connectivity. Reconnection requests a new snapshot instead of replaying the previous one.
10. Provide diagnostics, Prometheus text metrics, an example connector and an independent TLS conformance server. The suite starts an external executable through the documented fixture contract; it needs neither Kafka nor application services. Normal execution includes a real 600-second outage; short mode is for debugging only.

## Boundaries

Connector SDK provides the client library and protocol extension. Connector Gateway implements durable registration, Redis sessions, server heartbeat enforcement, revocation, quotas and command routing under ADR-049. The initial laboratory gateway used protocol 1.0 and ADR-047 limits, including an event time window; ADR-049 replaces those limits with full server processing. ADR-049, ADR-052 and ADR-059 define server, Normalizer and projection handling of snapshots and freshness metadata. Source types define domain observation transformations and freshness policies.

## Verification

Unit tests cover the journal and state model. Real gRPC/mTLS sessions test zero credits, lost acknowledgement, invalid `BatchAck`, permanent rejection, repeated commands, expired deadlines, configuration updates, timeouts, an untrusted CA and events arriving during snapshot acquisition. A separate run survives a ten-minute outage and client restart, delivering the original persisted identifiers without loss. Commands and the fixture contract are in the [SDK guide](../engineering/connector-sdk.md).
