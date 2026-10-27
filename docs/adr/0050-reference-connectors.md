# ADR-050. Reference connectors and durable HTTP retries

<!-- language-links -->
[English](0050-reference-connectors.md) | [Русский](../ru/adr/0050-reference-connectors.md)
<!-- /language-links -->

- Status: Accepted
- Related decisions: ADR-017, ADR-042, ADR-048, ADR-049

## Context

Vendor-independent sources are needed for command and load testing. The direct test client checks intentional re-sending and end-to-end reconciliation; it does not have a durable SDK buffer. An incoming HTTP request can be replayed after a response is lost or a process is restarted.

## Solution

1. Provide `psim-simulator` and `psim-webhook` as separate programs and Debian packages. Both use the public Connector SDK and mTLS with the gateway. The direct client for end-to-end reconciliation should be moved to `tools/event-flow/psim-flow-simulator`; Compose uses it for the original re-sending scenario.
2. The simulator creates a limited number of virtual devices, with scenarios `alarm`, `fire`, `offline`, `mixed`, `burst`. Each event contains `generator_run` and `generator_seq` for independent verification. Speed is determined by an open schedule; generator delay is measured, and event time matches the schedule. With `--rate 0`, the maximum packet generation speed is measured.
3. Add SDK atomic `publish_batch`: no more than 1000 events, one log entry, and one `fsync`, after validating the complete batch before writing. UUIDv7 and sequences remain the responsibility of the SDK. Transfer limits are set within the protocol, credits, and message size. By default, 32 events and 64 KiB are saved.
4. Add SDK `publish_once` for historical events with an external sender key. Original fields, assigned identity, and acknowledgment are saved in one log entry. Replay returns the previous acknowledgment after gateway confirmation, compaction and restart. Changes to content with the same key are rejected. Keys are not automatically overwritten; filling the registry stops accepting new keys. This eliminates the window between the separate HTTP registry and event log.
5. The webhook accepts HTTPS TLS 1.3 with a Bearer token from a secure file. One instance and token per connector registration; different external systems use separate instances and directories. Only `POST /v1/events` is accepted, with limited JSON and headers, required `Idempotency-Key` and original observation time. Duplicates of JSON fields and unknown fields are rejected. HTTP 202 indicates durable local reception, not confirmation to Kafka. Disk errors and filling result in 503; undefined results are replayed with the same key and content.
6. The webhook does not announce `commands.v1` and `snapshot.v1`. HTTP events are sent as history, without confirming the status of equipment. The simulator announces commands and snapshots; the SDK protects against repeated execution. The status of virtual equipment exists in process memory, and is rechecked after restart.
7. HTTP uses four working threads, no more than 64 connections, separate strands per connection, handshake/read/write deadlines of 5 seconds, 8 KiB headers and 64 KiB body. Diagnostics and metrics are available with the same authentication. Stopping closes reception and gives the SDK 5 seconds to send; remaining events remain in the log.
8. Both actual executable files must be checked for compliance. HTTP-fixture sends events through a real HTTPS endpoint; capabilities are not replaced. A separate TLS receiver checks all load events and source order. A minimum threshold of 75,000 events/second applies to durable generation on one machine; delivery time and number of confirmations are published separately. Performance qualification of the entire platform remains a separate check.

## Alternatives

- Reuse the direct client without SDK: lacks durability, automatic recovery and the guarantees of reference connectors.
- Separate HTTP registry before or after `publish`: gaps between records cause loss or duplication.
- Limited time for key caching: after clearing, it’s impossible to guarantee previous acknowledgment. Automatic key deletion requires a separate agreed protocol for replay termination.
- SQLite for incoming requests: the second log makes atomicity and recovery difficult; the current SDK log is already a point of durable reception.

## Consequences

The HTTP log grows with the number of unique keys. Its size must be monitored, and a new instance with new key space must be planned when quotas are exhausted. Deleting the working directory to free space is not allowed. New event records use Protobuf in Base64 within the verified JSON payload; reading previous JSON events is preserved. This reduces serialization cost during packet generation. Returning to the old SDK with a new log is not supported.

Durable state of virtual devices and MQTT/ONVIF connectors are outside this connector scope.
