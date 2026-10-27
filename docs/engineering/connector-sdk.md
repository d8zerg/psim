# Developing a connector with Connector SDK

<!-- language-links -->
[English](connector-sdk.md) | [Русский](../ru/engineering/connector-sdk.md)
<!-- /language-links -->

[ADR-048](../adr/0048-connector-sdk.md), [protocol v1](../../contracts/proto/psim/connector/v1/connector.proto).

## Linking the library

Build the SDK for Debian 13 x86_64 with the project toolchain. C++23, gRPC/Protobuf and the project dependencies are required. In the monorepo, link the CMake target `psim::platform_connector`:

```cmake
add_executable(my-connector main.cpp)
target_link_libraries(my-connector PRIVATE psim::platform_connector)
psim_target_defaults(my-connector)
```

The public headers are `psim/platform/connector/client.hpp`, `state.hpp` and `grpc_process.hpp`. The [basic.cpp](../../libs/platform/connector/examples/basic.cpp) example uses only the public SDK API. Building it does not require knowledge of the core services' internals.

```cpp
namespace sdk = psim::platform::connector;
namespace wire = psim::connector::v1;
const sdk::GrpcProcess grpc;
sdk::Client client({.endpoint = "gateway.example:8443",
                    .ca_file = "/etc/my-connector/ca.pem",
                    .certificate_file = "/etc/my-connector/client.pem",
                    .private_key_file = "/run/secrets/client.key",
                    .buffer_directory = "/var/lib/my-connector/buffer"});
auto started = client.start();
if (!started) {
  // Record the catalog error code and stop accepting upstream events.
  return 1;
}
wire::ConnectorEvent event;
event.set_source_ref("door-1");
event.set_raw_code("ALARM");
auto stored = client.publish(std::move(event));
if (!stored) {
  // Apply backpressure; do not acknowledge the external source.
  return 1;
}
const bool drained = client.flush(std::chrono::seconds(30));
client.stop();
return drained ? 0 : 1;
```

Create one `GrpcProcess` in `main`, before any use of gRPC or OpenSSL, and destroy it last. Like gRPC, the SDK initializes OpenSSL with `OPENSSL_INIT_NO_ATEXIT`: the global cryptographic context remains alive until process exit. The client closes its own RPCs and channels; a library destructor must not destroy global cryptographic state. A successful `start` means that the client has started; `diagnostics().connected` reports the actual connection. `publish_batch` atomically persists up to 1000 events with one `fsync`. `publish_once` persists an external-key receipt together with the historical event and returns that receipt on retries, including after a restart or a gateway acknowledgment. The registry is bounded by `max_idempotency_keys`; keys are not evicted. Reusing a key with different content is rejected.

`publish` is allowed before startup and while disconnected. Success means that the event is stored on local disk; Kafka persistence is acknowledged later through `BatchAck`. `publish` is synchronous: call it from the connector thread or a blocking pool, not a platform shard. `flush` waits until all events are removed following acceptance or permanent rejection. Connect `Callbacks::rejection` to diagnostics or your rejection store. Successfully draining the buffer does not, by itself, mean that the gateway accepted every event.

## Durability and limits

Use a persistent local directory owned by the process user. Do not share it between instances or delete it on restart. Directory permissions must be `0700` and file permissions `0600`; a second process cannot open the same directory. Private keys follow the [security library](../../libs/platform/security/README.md) rules.

The SDK assigns a UUIDv7 `event_id` and `source_seq`; do not populate these fields yourself. The first epoch is 1, and the persisted sequence continues after restart. To reset a source's numbering, provide a higher `source_epoch`; a lower epoch is rejected. The SDK fills in event time if the source omitted it. When equipment provides its own timestamp, preserve it rather than replacing it with retransmission time.

Defaults are a 64 MiB journal, up to 100,000 unacknowledged events and up to 10,000 command identifiers. Configure limits through `Options`. An event is limited to approximately 64 KiB. If the negotiated gateway limit is smaller than the first queued event, the SDK retains the event and reports that sending has stopped. Correct the limit or the input; events are never silently removed. Journal compaction requires space for a second file. Removing acknowledged events does not reset sequences or the command registry.

New versions can read earlier journal records. Batch records, Protobuf/Base64 records and the key registry require a newer SDK. Rolling the binary back to an earlier version with such a journal is unsupported.

Disk errors, insufficient space and buffer overflow must stop acknowledgments to the upstream source. After a write or `fsync` error, stop and destroy the `Client`, repair the disk, and create a new client with the same directory. A write with an unknown outcome may already be in the journal: inspect persisted data before resubmitting the event. A corrupt complete record causes opening to fail; it is not permission to skip that record. Durability assumes correct `fsync` and atomic rename behavior on the local filesystem.

## Sessions and flow control

The SDK sends `Register` first, offers versions `1.1` and `1.0`, and provides the connector version, process identifier and known configuration version. Sending respects `initial_credits`, additional `Credits`, and event count and size limits. Only one `BatchAck` is outstanding at a time. An invalid or incomplete acknowledgment closes the session without deleting events. Heartbeats continue even with no credits. Registration, session and batch acknowledgment timeouts bound stalls; `stop` cancels the active RPC and preserves unsent events.

`reconnect_min` and `reconnect_max` control exponential reconnect backoff with jitter. TLS verifies the CA, server name and client identity; TLS 1.3 is the minimum. The API provides neither server-name overrides nor insecure transport. To rotate a certificate, stop and destroy the previous `Client`, then create a new one with the updated files and the same buffer directory.

`Callbacks::config` receives only increasing `config_version` values during a client's lifetime. After restart, the version is requested again, so configuration application must be idempotent. Configuration and permanent-rejection handlers run on the session thread and must return promptly. Manage the client lifetime from its owning thread: a handler requests shutdown from the owner instead of calling `stop` or destroying the client inside the callback.

## Commands

Providing `Callbacks::command` advertises `commands.v1`. The handler receives a command and a `std::stop_token`; it checks the supported `capability`, equipment parameters, deadline and cancellation. It runs on a separate worker thread without delaying heartbeats. Return a `CommandResult` with `success`, and an [error catalog](../../contracts/errors/errors.yaml) code on failure. The SDK sets the command identifier. The result message is limited to 1024 bytes and must not contain secrets.

Before invoking the handler, the SDK synchronizes an execution-start record to disk. It synchronizes the result afterward. A duplicate does not invoke the handler again. If the process terminates between these records, the SDK returns `COMMAND_INVALID_STATE` with an unknown outcome. Inspect the equipment; do not automatically repeat the action. A deadline that expires before execution returns `COMMAND_EXPIRED`.

The command registry is not automatically cleared. Once full, it blocks new commands while preserving duplicate-execution protection. Size it appropriately and monitor errors. Deleting the directory removes durability for events, numbering and command protection.

## State reconciliation and availability

`Callbacks::snapshot` polls equipment after connection if the gateway negotiated `state.v1` and `snapshot.v1`. `state.v1` identifies the observation format; `snapshot.v1` is advertised only when a snapshot handler exists. The argument contains sequence boundaries captured before the request. Return only sources that were actually polled, including observation time, availability and values. The SDK replaces each response cursor with the saved boundary. The handler also receives a `std::stop_token`, runs on a worker thread, and must respect cancellation and finish within a bounded time. A snapshot supports up to 1000 sources. Messages are automatically split at 64 KiB and wait for space in a bounded control queue. A missing source remains `unknown`; snapshot completion does not mean that all equipment is healthy.

For a regular observation, send availability and values through `Client::report`. The method returns an error if `state.v1` was not negotiated, no session is active, or the queue has no room. Never present persisted old state as a new equipment observation.

The SDK fills in `live_observation` and `observation_session_id`. By default, `publish` accepts historical data. For a directly received current observation, pass `Observation::kCurrent` as the second argument. Imported history must not receive this flag even while connected. An event sent through a different session gets `live_observation=false` while retaining its original timestamp and identifier. History does not prove freshness. The receiver uses epoch/sequence ordering and the snapshot boundary: an event received during the request takes precedence over an older snapshot. `StateTracker` provides a testable model of this behavior, separate `unknown`, `actual` and `stale` qualities, and explicit freshness expiration.

Connector Gateway negotiates protocol 1.1, accepts history regardless of age, and filters snapshots against newer observations. Registration, revocation and server checks are described in the [gateway guide](connector-gateway.md). State normalization is covered in [normalization and history](normalization-and-history.md), and console state reads in [projections](console-projections.md).

## Metrics and diagnostics

`diagnostics()` reports connection state, buffer depth, journal size, session counts, accepted and rejected event counts, and the last error. `metrics()` emits Prometheus text with the `psim_connector_` prefix; expose it through your diagnostics endpoint. A platform connection does not prove that every device is available. The number of successful `publish` calls is not the number of Kafka acknowledgments.

## Conformance suite

Run the full check, including an actual 600-second outage:

```bash
task connectors:conformance
```

Use the shorter check for debugging; it does not satisfy the ten-minute outage acceptance criterion:

```bash
task connectors:conformance OUTAGE_SECONDS=1
```

The suite first runs SDK unit tests, then a separate TLS server and the example connector. The server grants zero credits until a heartbeat, limits batches to two events, disconnects before acknowledgment, and requires retransmission with the original identifier and sequence without the current-observation flag. After recovery, it checks duplicate and expired commands, versioned configuration, a fresh snapshot and a permanent rejection for one event. Artifacts are in `build/connector-conformance/<preset>/example`: `client.log`, `peer.log` and `report.json`.

For another executable, run inside the toolchain:

```bash
python3 -I tools/connectors/conformance.py --outage-seconds 600 -- /repo/build/my-connector --conformance
```

External-client fixture contract:

- Read the endpoint, CA, certificate, key, separate buffer directory and wait timeout from `PSIM_CONNECTOR_ENDPOINT`, `PSIM_CONNECTOR_CA_FILE`, `PSIM_CONNECTOR_CERTIFICATE_FILE`, `PSIM_CONNECTOR_PRIVATE_KEY_FILE`, `PSIM_CONNECTOR_BUFFER_DIRECTORY` and `PSIM_CONNECTOR_FLUSH_TIMEOUT_SECONDS`.
- Offer protocol 1.1 and capabilities that are actually supported. Send 20 events from `door-1` at 50 ms intervals: the first 19 with `raw_code=ALARM`, the last with `raw_code=REJECT`.
- If commands are advertised, support `arm` for `device-1`, execute it once, return the stored result for a duplicate, and reject an expired deadline. If `snapshot.v1` is advertised, poll `door-1` again after connection.
- Stay connected until responses are checked and the buffer is drained, then exit with code 0. The fixture directory is isolated and removed only after all child processes finish.

The C++ `ConnectorConformance.TenMinuteOutage` scenario additionally restarts the client object during the outage and compares the set of original identifiers. `PSIM_CONNECTOR_OUTAGE_SECONDS=600` sets the actual duration; without it, the test uses a one-second smoke run. Snapshot ordering, old history, termination without destructors and journal corruption tests require no external services.
