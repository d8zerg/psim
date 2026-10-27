# Correlation Rules and Historical Evaluation

<!-- language-links -->
[English](correlation-rules.md) | [Русский](../ru/engineering/correlation-rules.md)
<!-- /language-links -->

## Launch

The Correlation Engine has two main deployment roles: `settings.role: live` for the live processing and `management` for HTTPS API rules and historical executions. One process launches one role. Management uses one shard per instance; multiple instances work together through PostgreSQL and outbox. These roles represent existing services, not separate microservices.

The launch test environment is initiated with the command `task rules:test`. Its configuration is `deploy/local/config/rules/management.yaml`, and its Compose profile is `rules-test`. The command raises a project with two management instances, checks the actual storage and broker, and saves the report in `build/rules/debug/`. The project can be specified via `PROJECT`, and the build via `PRESET`.

HTTPS is used for delivery, and JWT verification with an audience of `psim` is performed. TLS-protected storage and a separate Kafka read certificate are used for replay. `allow_plaintext: true` is designed for local test environments. The taxonomy is provided with the service in `/usr/share/psim/correlation-taxonomy.yaml`.

## Lifecycle and Access

Saving creates an immutable version in the `draft` state; the input document state is ignored. Successful validation moves the version to `testing`. Activation accepts only successfully verified versions and atomically replaces the tenant set. Archived versions become `archived`. Saving a new draft does not change the active version. Archiving a rule removes all its versions from the active set and publishes the new set.

Changes are accessible to the administrator with a global access scope of `*`. Reading and historical execution are accessible to admin, integrator, supervisor, and auditor within their scopes. For limited-site scopes, the rule must explicitly list allowed sites. The execution result is visible to the owner or global administrator; reading rechecks the current access scope. Data and tasks from other tenants are not accessible.

All POST requests require a UUID in `Idempotency-Key`. A repeat returns the previous result; another request with the same key results in 422. Changing an existing rule requires `If-Match` with the last version number, and activation requires the version set. Missing headers result in 428, outdated versions in 412. Invalid documents result in 400, invalid state transitions in 409. The set limits to 1000 rules and a configuration message size of 1 MiB.

Changes, audits, and configuration messages are saved as a single PostgreSQL transaction. Outbox publishes the complete set in `psim.config.v1` and audits in `psim.audit.v1`. Redelivery is possible, and the message identifier is retained. Deleting critical rules is noted in the audit for subsequent notification.

## Expressions

A JSON or YAML document matches `CorrelationRule`. The format of the rule, window, filters, and scope are declared. Conditions, priorities, and text substitutions use a typed subset of CEL from [ADR-011](../adr/0011-correlation-rule-language.md).

`bool`, `int64`, `string`, `has(field)`, `&&`, `||`, `!` are supported, along with comparisons, integer arithmetic, `condition ? a : b`, string concatenation, `startsWith`, `endsWith`, `contains`. String attributes are read as `event.attributes['key']`; necessity is checked through `has`. Types are checked before activation; no implicit conversions are allowed. Loops, regular expressions, and external calls are not allowed.

Examples: `event.severity >= 3 && has(event.attributes['door'])`, `event.severity >= 3 ? 1 : 2`, text `Zone ${event.zone_id}`. The priority expression must statically guarantee a result of 1..4. `matches.count` is not available in filters; this value is available to output expressions. Text substitutions must return a string.

The document limits to 64 KiB, the expression to 4000 bytes, AST to 512 nodes with a depth of 32. String values are limited to 4096 bytes, and the final signal text to 1024 bytes. Calculations are limited by a budget of 4096 operations, considering the cost of string operations. Overflow, division by zero, and absence of mandatory fields result in errors. YAML prohibits anchors, aliases, repeated keys, and multiple documents; a depth of 32 and 4096 nodes is allowed.

## Types of Rules

| Type | Semantic Description of Historical Execution |
|---|---|
| `single` | Each matching input produces a decision considering the repetition suppression interval |
| `conjunction` | Each part requires a suitable event in a single window; an event does not occupy two parts |
| `sequence` | Parts are executed in a specified order with increasing time of events |
| `threshold` | The threshold for the number of events or different values of a field is reached in a window |
| `absence` with trigger | A trigger opens a waiting state; absence of a suitable event before the limit produces a decision |
| `absence` without trigger | Active resources in the catalog open periodic waiting periods, starting from the beginning of the execution; absence can signal without events |

Windows are half-open: an event exactly at the boundary no longer closes the waiting state. The end of the execution boundary is also excluded. After a successful composite match, used events are consumed; sequences and combinations do not overlap. For periodic absence, resource filters and output use only inventory fields, rule metadata, and the number of matches. Values for severity, type, and attributes of an absent event are not available. Constraints apply during validation.

The correlation key is selected from a source, device, zone, floor, building, or site. The grouping scope cannot be narrower than the correlation scope; the grouping key also contains the incident type. The signal identifier is determined by the tenant, rule, version, key, events, and absence boundary. Duplicates of events are removed by identifier; different data with the same identifier ends the task with an error. Processing is ordered by event time and identifier.

## Historical Data and Maintenance

Launch takes a historical range up to 24 hours and a Kafka or ClickHouse source. The initial state of correlation is empty. For comparing sources, one range, rule, and catalog context are assigned. Execution does not model the live engine state before the start of the range, watermarks, or allowed lateness.

Kafka is read using manual partition assignment and `read_committed` up to captured upper offsets, without joining a consumer group or committing offsets. The reader is limited to normalized events, catalog, configuration, and audit. Insufficient retained history is rejected. ClickHouse runs a parameterized SELECT of complete events only from `events`, with mandatory tenant and time bounds. Addresses, tables, and topics supplied through HTTP requests are not accepted.

The catalog is fixed during input reading. Historical maintenance policy versions are restored from configuration and audit snapshots, and selected by the time of event receipt; a waiting period is used for timers. If the desired historical version is unavailable, the process ends with an error. All composite signal events must be covered by the policy. Suppression of broad expectations due to a single device’s policy is not allowed.

Suppressed decisions retain signal, identifiers, and exact versions of maintenance policies. Fire events and P1 pass regardless of the policy. A suppressed decision does not start a repetition suppression interval so that a new event can create a signal after maintenance ends. The working history remains unchanged.

## Tasks and Isolation

A task is persistent: one executor captures it with a lease of 15 seconds and updates the lease every 2 seconds. Saved input is reused after restarting; an outdated executor cannot record results. A snapshot contains events and context, not JWTs. An error does not leave partial decisions; the API returns a safe cause code.

Process limits: up to 100,000 events, 10,000 keys, 10,000 decisions, up to 50 million matching operations, up to 10 pending tasks per tenant. The input snapshot is limited to 8 MiB, and the result to 16 MiB. Kafka additionally limits reading to one million messages and a time limit of 45 seconds. Exceeding any limit ends the task with an error; exceeding the quota gives 429.

`signalsProduced` counts all decisions, including those suppressed by maintenance. `signals[]` contains only non-suppressed simulated signals from the current page, `decisions[]` - all decisions from the current page with suppression explanations. Pagination returns up to 200 decisions per request.

The historical reader uses separate Kafka ACLs without write permission and without committing group offsets, and the ClickHouse user only has SELECT rights. Management has only publication rights for configuration and auditing and uses its own PostgreSQL schema. Verification actually tries to record signals, incidents, commands, and action results to topics, commit offsets, change another PostgreSQL schema, and record in ClickHouse. All operations must be rejected. The process does not create production signals, incidents, commands, or notifications and does not trigger external systems.

## Verification

- `task rules:test` - HTTPS/JWT, versions, immutability, concurrency between two instances, all types of rules, both sources, maintenance, isolation, and restoration of tasks after SIGKILL.
- `task rules:sanitizers` - unit and live-process checks under ASan/UBSan and TSan.
- `task rules:coverage` - coverage by unit and live-process checks; domain thresholds of 90% lines and 80% branches, service - 75% lines.
- `task fuzz:rules` - minimum 600 seconds of parser and validator fuzzing under ASan/UBSan, report in `build/rules/fuzz/`.

These checks are connected to the CI by affected components. CI performs 10 minutes of fuzzing. Qualification release requires 24 hours for each external parser and DSL target. The published live rule set, RocksDB/changelog, watermarks, and Kafka transactions are described in the [live correlation guide](live-correlation.md).
