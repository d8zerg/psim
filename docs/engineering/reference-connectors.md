# Reference Connectors

<!-- language-links -->
[English](reference-connectors.md) | [Русский](../ru/engineering/reference-connectors.md)
<!-- /language-links -->

Solutions: [ADR-050](../adr/0050-reference-connectors.md), [SDK](connector-sdk.md), [Gateway Registration](connector-gateway.md).

## Build and Connection

```bash
task cpp:build PRESET=release
```

Programs: `build/debian13/release/connectors/simulator/psim-simulator`, `build/debian13/release/connectors/webhook/psim-webhook`. Packages: `psim-simulator`, `psim-webhook`. Commands are executed in the project toolchain or after installing Debian packages.

Both programs read:

| Variable | Purpose |
|---|---|
| `PSIM_CONNECTOR_ENDPOINT` | Registered gateway gRPC endpoint |
| `PSIM_CONNECTOR_CA_FILE` | Gateway CA certificate |
| `PSIM_CONNECTOR_CERTIFICATE_FILE` | Registered client certificate |
| `PSIM_CONNECTOR_PRIVATE_KEY_FILE` | Secured private key file |
| `PSIM_CONNECTOR_BUFFER_DIRECTORY` | Separate persistent SDK directory |
| `PSIM_CONNECTOR_FLUSH_TIMEOUT_SECONDS` | Simulation sending timeout, default 120 seconds |

The certificate must be registered via the gateway’s administrative CLI. The directory is retained upon restart, has a single owner, and is not shared among programs. Sending resumes automatically if communication is lost; quota exhaustion stops accepting.

## Simulator

```bash
psim-simulator --buffer /var/lib/psim-simulator --devices 10000 \
  --count 500000 --scenario mixed --run-id test-1
psim-simulator --buffer /var/lib/psim-simulator --devices 5000 \
  --count 100000 --rate 10000 --scenario burst --burst-every 10000 --burst-size 1000
```

`--scenario`: `alarm` -> ALARM, `fire` -> FIRE, `offline` -> OFFLINE, `mixed` -> three code cycles. In `burst`, the first `burst-size` events of each period are assigned to a single moment; the rest follow an average rate of `--rate`. The code in `burst` is ALARM. `--manifest <path>` saves UUIDs from event logs, one per line for later comparison. `--source-prefix` sets the prefix for sources, defaulting to `device-`.

Limitations: 100,000 devices, 100 million events, 1,000 events per local batch. `--batch` defaults to 500. If `--rate` is non-zero, events are entered one at a time according to the open-loop schedule. With `--rate 0`, generation is not limited by schedule and uses SDK batch storage. The simulator buffer is limited to 512 MiB and 1 million unacknowledged events.

An event carries `generator_run` and global `generator_seq`; the SDK assigns UUIDv7, epoch, and number to each source separately. The final JSON line contains lifetime generation rate, delivery time, acknowledgments, failures, remaining, and maximum schedule delay. Permanent failure is logged as stderr with a non-zero exit code.

Commands `arm` and `disarm` target `device-N`, without parameters. They check device availability, supported capability, cancellation, and deadline. The snapshot includes availability and `armed` value for the first 1,000 sources within the SDK; others remain unknown. In `offline` scenario, the snapshot shows offline. Virtual equipment status is stored in memory; the SDK command log survives restart and prevents re-execution.

The direct end-to-end test client is located at `tools/event-flow/psim-flow-simulator`. It’s used by Compose for intentional repetitions and trace measurement. The model testbed `libs/sim` runs separately.

## Webhook/HTTP

In addition to SDK settings, the following are required:

| Variable | Purpose |
|---|---|
| `PSIM_WEBHOOK_TOKEN_FILE` | Bearer token file, 32-256 bytes without spaces or line breaks, permissions 0400/0600 |
| `PSIM_WEBHOOK_CERTIFICATE_FILE` | Server HTTPS certificate |
| `PSIM_WEBHOOK_PRIVATE_KEY_FILE` | Secured HTTPS private key |
| `PSIM_WEBHOOK_ADDRESS` | Listening address, default 127.0.0.1 |
| `PSIM_WEBHOOK_PORT` | Port, default 8443 |

```bash
psim-webhook
```

`POST /v1/events`, `Content-Type: application/json`, `Authorization: Bearer <token>`, `Idempotency-Key: <key>`. Format:

```json
{
  "source_ref": "door-1",
  "raw_code": "ALARM",
  "occurred_at": "<RFC3339 timestamp>",
  "attributes": {"upstream": "alarm-123"}
}
```

The first three fields are required; `attributes` is an optional string object. Unknown and repeated fields are rejected. `source_ref` is up to 256 bytes, `raw_code` is up to 128, with 64 attributes with keys up to 256 and values up to 4096 bytes. The body is limited to 64 KiB; serialized events are 64,000 bytes. Source time is required; history does not receive current observation status. The HTTP client cannot assign UUIDs or source sequences; the SDK assigns them.

The key contains 1-128 ASCII letters, digits, or `-_.:`. It’s unique within the permanent connector directory. 202 and `{event_id, source_epoch, source_seq, status: "buffered"}` are returned after `fsync`; if the gateway is unavailable, events remain in the log. This is not yet a Kafka acknowledgment.

| Response | Action from sender |
|---|---|
| 202 | Save acknowledgment; repetition with same key and content will return it again |
| 400 / 415 | Correct format; data not accepted |
| 401 | Correct authentication |
| 409 | Key already used with other content; check source |
| 503 / lost response | Repeat with previous key and content, with exponential delay and jitter |

Changing the order of JSON fields does not change the content. The key registry is saved after gateway acknowledgement, compaction, and restart; default limit is 100,000 keys and a total SDK quota of 64 MiB. Old keys are not removed. When filled, new keys get 503; existing repetitions continue to return acknowledgments. Quotas and scheduled instance and key space management are needed. Deleting the working directory violates guarantees.

HTTP itself does not confirm device availability or execute commands. Webhook does not announce `commands.v1` or `snapshot.v1`; device quality remains unknown until separate confirmed observation.

`GET /healthz` shows connection and buffer depth; `GET /metrics` - SDK metrics. Both endpoints require the same Bearer token. SIGTERM stops receiving, waits for a response up to 5 seconds, and retains the remaining. Final gateway failures are logged in stderr with identifier and code; the sender must compare them with acknowledgments.

Example sender: [send_webhook.py](../../tools/connectors/send_webhook.py). It reads JSON from file, token from secured file, checks CA, and repeats only network errors and 503 with same key.

## Testing

```bash
task connectors:conformance                    # SDK example and both connectors, 600-second outages
task connectors:conformance OUTAGE_SECONDS=1   # Short diagnostic run
task connectors:bench                          # Release build, 75,000 events/s generation gate
task connectors:coverage
task connectors:sanitizers
# With the local environment running:
task connectors:integration
```

HTTP-fixture uses a real HTTPS server, checking authentication, validation, repetitions, key conflict, recovery, and permanent failure. The simulator also tests commands and snapshot freshness. Compliance artifacts: `build/connector-conformance/<preset>/{example,simulator,webhook}`.

Before measuring the generator, complete other builds and load tests on the same machine.

The load receiver checks mTLS, credits, batches, the complete set of `generator_seq`, and the order of each source. The report `build/reference-connectors/release/load/result.json` separately shows the generation speed and delivery time. This measurement checks the generator and transport protocol, not the throughput of Kafka or the entire platform.
