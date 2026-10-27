# ADR-059. Console read models in Projection Service

<!-- language-links -->
[English](0059-console-projections.md) | [Русский](../ru/adr/0059-console-projections.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-019](0019-realtime-channel.md), [ADR-047](0047-event-flow.md), [ADR-057](0057-incident-reporting.md), [ADR-058](0058-api-gateway-and-configuration.md)

## Solution

Projection Service maintains PostgreSQL models for incidents, catalogs, observations, connectivity, maintenance and duty assignments. Model state and Kafka offsets are updated in one transaction. The API Gateway forwards snapshot requests via HTTPS with the original JWT; access scope authorization is performed by the model owner. The gateway only stores a limited realtime log and outbox of access failures.

One process updates the overall model; PostgreSQL’s advisory lock prevents parallel writers and rebuilds when writing is ongoing. Additional processes handle reading. ClickHouse reporting and console models are activated independently.

Redis stores limited snapshots of raw data by generation, revision, and tenant. Revisions are read from PostgreSQL using Repeatable Read; errors or cache misses lead to PostgreSQL reading. Access scopes, maintenance intervals, freshness, and current connector lease are calculated with each request. Cached observations do not confirm equipment availability.

Rebuilds occur with the writer stopped, into a separate generation from read_committed Kafka to the specified partition ends, including compaction gaps and transaction control records. Generation switching is atomic. If available retention does not allow restoring previous data, comparison checks prevent switching; the active generation is retained. To recover after complete PostgreSQL loss, Kafka with sufficient history or a backup is required. Current storage times for raw topics are not implicitly extended.

Observations store original time, epoch, sequence, and session. History and event information do not update state confirmation. Equipment availability is confirmed by a separate state report. Model generation and offset vector are included in the snapshot token. [ADR-060](0060-realtime-delivery.md) replaces the temporary journal binding to a Gateway epoch by restarting from Kafka and defining the flow of operational snapshots.

## Updates

First, Projection Service is started and its model is restored. Then, the Gateway is switched. Old tables `access.incident_feed`, `access.feed_offsets`, and `gateway_state` are not automatically deleted; new Gateway processes do not read or update them. Deletion is allowed after checking snapshots and closing the rollback window.

## Tests

Network tests verify snapshots and access scopes, Redis cleanup, recovery from Kafka, switching failures with incomplete history, freshness after recovery, and update delays. The end-to-end event flow is tested after removing the Gateway dependency on its own projections. Reporting tests remain in place.
