# ADR-018. Routing commands to connector sessions

<!-- language-links -->
[English](0018-command-routing.md) | [Русский](../ru/adr/0018-command-routing.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-015, ADR-017, [data-flows.md](../architecture/data-flows.md#36-commands-psimcommandsv1-and-psimcommandsresultsv1)

## Context

Command Service writes a command to `psim.commands.v1` with `connector_id` as its key. The command must reach the Connector Gateway instance holding the connector session. Multiple gateway instances exist, and reconnection can move a session to another instance. Command volume is a few per second; request -> delivery latency must be ≤ 200 ms.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| Consumer group with forwarding between instances using the Redis session registry | Each command is read once | Requires an inter-instance forwarding channel; Redis enters the critical path; session movement is complex |
| Pin each connector to a partition and instance using sticky load-balancer routing | No forwarding | The L4 load balancer must know the mapping; instance failure breaks it |
| **Broadcast consumption: each instance reads all partitions in its own group and delivers commands for sessions it owns** | Simple; no inter-instance channel; Redis is outside the delivery critical path | Every instance reads all commands, whose volume is small |

## Decision

Use broadcast consumption: a separate ephemeral group for each instance start, without committed offsets. Before opening its gRPC listener, the instance receives all partition assignments with `auto.offset.reset=latest`. Groups do not distribute commands between instances. Additionally:

1. Before sending, Command Service checks the session registry: an offline connector immediately produces `failed: COMMAND_CONNECTOR_OFFLINE`.
2. At startup, the gateway starts at the topic end. Commands older than the instance start are not delivered: they were either delivered already or will expire at their deadlines.
3. A command whose `deadline_at` has expired is not delivered.
4. Redelivery of the same `command_id` is safe (ADR-017, rule 4).

## Consequences

- Reconsider registry-based forwarding when reaching hundreds of gateway instances or thousands of commands per second.

## Verification

Test connector reconnection to another instance between request and delivery; measure p99 delivery latency.
