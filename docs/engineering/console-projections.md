# Console Read Models

<!-- language-links -->
[English](console-projections.md) | [Русский](../ru/engineering/console-projections.md)
<!-- /language-links -->

The Projection Service serves `/v1/feed/incidents`, `/v1/feed/operators`, and `/v1/feed/locations/{locationId}/status`. The API Gateway transmits the original JWT via HTTPS. The model owner checks the role, tenant, and access scope of the site or zone. The solution is described in [ADR-059](../adr/0059-console-projections.md).

## Storage and Freshness

PostgreSQL stores a shared catalog, incidents, observations, state reports, connection status, maintenance state, duty, and positions for six source topics. Resource updates, revisions, and positions are recorded in one transaction. Snapshots use Repeatable Read. Closed incidents are excluded from the active feed; operator workload includes assigned incidents, primary and backup duties.

Event observations and availability confirmation are stored separately. Historical or informational events do not confirm the current state. The report must cover the current epoch and sequence of sources. A report from a previous epoch or with a lower cursor does not replace current values. The lifecycle status of devices and connectors, current session, observed state, values, and quality are available independently.

The observation freshness is 60 seconds, with a tolerance for future time of 30 seconds. Old acknowledgments remain stale after restoration. Online requires a fresh state report from the current session and active resources. Explicit termination of the current session disables online, even if the lease still exists. Redis errors do not confirm availability.

Redis stores the source data of tenants by generation and revision for 5 seconds with a limit of 1 MiB. Authorization, maintenance state, freshness, and lease are calculated at each read. Cache errors or misses fall back to PostgreSQL. Reading is limited to 100,000 resources per tenant and 10,000 incidents; exceeding a limit returns an error instead of a partial snapshot.

## Configuration and Updates

`settings.console_enabled`, `settings.console_writer`, and `settings.reports_enabled` are enabled by default. For individual console writers, disable `reports_enabled`. For additional readers, disable `console_writer`; for individual reporting instances, disable `console_enabled`. PostgreSQL, Kafka, TLS listener, and JWT are configured as in the [reporting](incident-reports.md) section. `console_replay_timeout_ms` limits initial restoration to 300,000 ms by default.

`redis_host`, `redis_password_file`, `redis_ca_file`, `redis_certificate_file`, and `redis_key_file` are set by the Projection Service. TLS is required without an explicit local `allow_plaintext`. For snapshot caching, these parameters are handled by the Projection Service; API Gateway uses a separate Valkey connection for shared REST request quotas.

The writer holds PostgreSQL advisory lock on the allocated connection. The second writer is not ready. Losing the connection blocks writing; readiness depends on model loading and working consumers. First, start the Projection Service, then configure `projection_host` and `projection_port` Gateways and switch traffic. Before switching, verify active incidents and states with the previous Gateway; if Kafka has insufficient history, data transfer or restoration from a backup is required. Old `access.incident_feed`, `access.feed_offsets`, and `gateway_state` are not automatically deleted. After checking and closing the rollback window, they can be manually deleted; the `access` schema preserves gateway failure auditing.

## Rebuild

Stop the writer while keeping Kafka and PostgreSQL accessible. Run the provided utility in an environment with access to them:

```bash
PSIM_PG_PASSWORD_FILE=/run/secrets/postgres \
PSIM_KAFKA_BROKERS=kafka-1:9093 \
PSIM_KAFKA_CA_FILE=/run/tls/ca.pem \
PSIM_KAFKA_CERT_FILE=/run/tls/client.pem \
PSIM_KAFKA_KEY_FILE=/run/tls/client.key \
psim-console-tool rebuild
```

The utility uses PostgreSQL variables `PSIM_PG_HOST`, `PSIM_PG_PORT`, `PSIM_PG_DATABASE`, `PSIM_PG_USER`, `PSIM_PG_SSLMODE`, `PSIM_PG_SSLROOTCERT`, `PSIM_PG_SSLCERT`, `PSIM_PG_SSLKEY`. Local plaintext requires explicit `PSIM_KAFKA_ALLOW_PLAINTEXT=1` and `PSIM_PG_SSLMODE=disable`.

The utility gets the same lock, reads read_committed Kafka from the available history to saved positions, and creates a separate generation. After comparing all resources, it switches with one transaction. An active writer, incomplete history, or data differences cause the switch to be rejected. The active generation is saved. After success, start the writer: it will continue from the saved positions. The version 2 snapshot token contains the generation, tenant, issuance time, and positions of incidents and responses topics. Resumption does not depend on the Gateway process; for details, see [realtime](realtime.md).

Complete loss of PostgreSQL requires sufficient Kafka history or backup. Retention of source topics is limited; comparison protects the existing model from incomplete rebuilds, but does not restore deleted source events. Old generations are saved for diagnosis; automatic cleanup is not performed.

## Verification and Observability

```bash
task projection:test
task projection:bench
task projection:sanitizers
task projection:coverage
```

Verifications use real HTTPS/JWT, Kafka, PostgreSQL, and Valkey. They check access scopes, independent cursors, duty, device maintenance state, epoch change, connector disconnection, cache and lease loss, consistency of rebuilds, and rejection of incomplete history. Coverage additionally runs Catalog, reporting, the end-to-end baseline, and module tests.

Artifacts are `build/resource-catalog/<preset>/projection-result.json`, `projection-servers.log`, and `api-responses.json`. The histogram `psim_console_update_seconds` measures Kafka CreateTime -> PostgreSQL COMMIT for live updates; replay is excluded. The verification sends 300 updates and requires p99 not exceeding 100 ms in normal build. This is a local measurement, not confirmation of the platform’s full load. `psim_console_query_seconds`, `psim_console_cache_hits_total`, and `psim_console_cache_misses_total` show reading; Grafana panel and delay alert use these metrics.
