# ADR-054. Event History, links and extended retention

<!-- language-links -->
[English](0054-event-history-links.md) | [Русский](../ru/adr/0054-event-history-links.md)
<!-- /language-links -->

- Status: Accepted
- Related decisions: ADR-020, ADR-028, ADR-045, ADR-052, ADR-053

## Context

The existing loader stores normalized events with individual TTLs. To analyze an incident, it’s necessary to have the original events underlying signals, rule version statistics, and extended retention of events whose significance has been determined after normalization. A signal and an event can be entered into history in different orders. Incident data belongs to the Incident Service; Event History stores the reconstructed projection of relationships.

## Solution

1. Save the previous migration `events` and its loader log. A separate loader of the same process reads `psim.signals.v1` and `psim.incidents.events.v1`, stores the prepared native blocks in PostgreSQL, and writes `history_facts`. Each row corresponds to the input message; logical deduplication is performed by `ReplicatedReplacingMergeTree` and `FINAL`. Normalized events and facts have independent Kafka groups and transaction producer identifiers.
2. `signal_facts`, `event_signals`, and `incident_signals` are ordinary views of deduplicated facts. Relationships are reconstructed from an array of unique event_ids. This avoids a separate insertion point where facts and relationships could get mixed up after a failure. For incidents, explicit signal_id of creation and attachment events are used, including signals beyond the preview of 1000 elements.
3. An additional signal consumer idempotently stores extension requests in PostgreSQL. The processing packet locks rows through `SKIP LOCKED`, searches for event bodies in ClickHouse, and inserts significant copies into `retained_events`. Only after confirmation of insertion is the request deleted. A failure may repeat the physical write, but `ReplacingMergeTree` with a retention period version and `FINAL` store one logical result with the longest retention period. Repeats and shorter policies do not reduce already extended retention.
4. A signal that comes before an event leaves an awaiting request. Missing bodies are rechecked until the end of the significant storage period; after that, the request is deleted. Already deleted bodies cannot be restored from a single reference. The API returns unavailable event_ids separately. The original normalized topic remains unchanged; the significance indicator is clarified in the history copy.
5. The default duration for all events is 7 days, for significant events and relationships it’s 90 days. `RetentionSettings` applies to newly prepared blocks and new extension requests. The configuration is restored before processing begins. All tables with data have TTL; requests also exclude expired rows until physical deletion through background merging.
6. HTTPS API checks JWT and the general authorization library. Search, statistics, relationships, and bodies are always limited by the tenant and time, as well as sites and user zones up to pagination. Administrators do not automatically get operator rights. Role failures are written to the durable audit outbox. External routing is performed by API Gateway according to ADR-058.
7. The parameterized reading in ClickHouse only accepts constant, verified SELECT. Values of tenant and interval are reserved; filters are passed as native parameters. The result is limited in terms of rows, bytes, and time. The cursor is related to the request, subject, and access scope; it does not give additional rights.

## Alternatives

- A separate insertion for each relationship or materialized view with its own table complicates recovery after partial success and reinsertion. Representing facts retains consistency without additional distributed transactions.
- Frequent changes in TTL of original ClickHouse rows create additional merging work and require coordination with reloading. Separate copies of significant events have a monotonically increasing retention period.
- Synchronous event request during signal processing does not support reverse delivery order. The durable queue preserves waiting and survives restart.

## Verification

`task history:test`, `task history:sanitizers`, `task history:coverage`, `task history:bench`. Verifications use two real services: Kafka, PostgreSQL, ClickHouse with Keeper and Keycloak. Search within a day is measured while writing at least 50,000 events per second. A short local run does not replace long-term verification of cluster delivery.
