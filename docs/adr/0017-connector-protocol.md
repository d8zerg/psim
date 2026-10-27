# ADR-017. Connector protocol and flow control

<!-- language-links -->
[English](0017-connector-protocol.md) | [Русский](../ru/adr/0017-connector-protocol.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-004, ADR-006, ADR-018, [state-models.md](../domain/state-models.md#4-connector-session)

## Context

Integrator connectors send events and execute commands. The protocol must authenticate connectors, prevent event loss through acknowledgement after Kafka persistence and retransmission after disconnection, protect the platform from overload, deliver commands and configuration, support any implementation language without requiring the SDK, and evolve without breaking existing connectors.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| REST/HTTP POST batches | Widely supported | Commands require a reverse channel, such as long polling or webhooks to the connector; no flow control |
| MQTT | Popular in IoT | Adds an MQTT broker to the distribution; QoS 2 does not guarantee delivery all the way to Kafka; weak payload typing |
| Direct connector writes to Kafka | No gateway | Connectors gain bus access; no validation, limits or source-ownership checks; external systems depend on internal topology |
| WebSocket with JSON | Simple | Custom acknowledgements and flow control; larger JSON payloads |
| **gRPC: bidirectional session stream, Protobuf, mTLS and credit-based flow control** | Types and code generation across languages; one stream for events, acknowledgements, commands and configuration; HTTP/2 and TLS 1.3 | Requires connector-side gRPC libraries, available for all major languages |

## Decision

Use `psim.connector.v1.ConnectorGatewayService` with `Session(stream SessionRequest) returns (stream SessionResponse)`.

| Message | Direction | Purpose |
|---|---|---|
| `Register` | connector -> gateway | First message: supported protocol versions, connector version, process identifier, configuration version and capabilities |
| `Registered` | gateway -> connector | Session identifier, negotiated version, initial credits and settings: heartbeat interval and maximum batch |
| `Heartbeat` | both directions | Every 5 s; session timeout 15 s |
| `EventBatch` | connector -> gateway | Batch sequence; up to 1,000 events or 1 MB |
| `BatchAck` | gateway -> connector | Batch persisted to Kafka (`acks=all`) or rejected with per-event codes |
| `Credits` | gateway -> connector | Credits measured in events |
| `Command` | gateway -> connector | Device command: `command_id`, device, capability, parameters and deadline |
| `CommandAck`, `CommandResult` | connector -> gateway | Receipt and execution result |
| `ConfigUpdate` | gateway -> connector | Catalog connector configuration: devices and sources |
| `StateReport` | connector -> gateway | Current observations or a requested snapshot after capability negotiation; missing sources remain unknown |
| `Goodbye` | both directions | Graceful termination with a reason, such as draining or replacement |

Rules:

1. **Authentication:** mTLS; the certificate fingerprint maps to a catalog connector registration (C1, C2).
2. **Flow control:** a connector sends no more events than its granted credits. Credits return as writes are acknowledged. HTTP/2 flow control also applies.
3. **Acknowledgement:** only after `acks=all`. The connector resends unacknowledged events in a new session with the same `event_id` and `source_seq`.
4. **Commands:** the connector executes each `command_id` at most once and returns the saved result on redelivery.
5. **Versions:** package `psim.connector.v1`; negotiate the version in `Register`; the implementation supports 1.1 and compatibility mode 1.0. State and snapshot capabilities are negotiated separately; historical events cannot replace current observations ([ADR-048](0048-connector-sdk.md), [ADR-049](0049-connector-gateway.md)).
6. **Errors:** gRPC statuses plus `google.rpc.ErrorInfo` containing a catalog code.

## Consequences

- The C++ Connector SDK implements the protocol, disk buffer and retries. Integrators using other languages use a generated client and the conformance suite.
- The protocol is defined in `contracts/proto/psim/connector/v1`.

## Verification

The conformance suite checks each protocol rule; fuzzing checks gateway message decoding.
