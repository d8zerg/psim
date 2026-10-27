# ADR-002. Kafka as the central bus: distribution and version

<!-- language-links -->
[English](0002-kafka-central-bus.md) | [Русский](../ru/adr/0002-kafka-central-bus.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-006, ADR-007, [data-flows.md](../architecture/data-flows.md)

## Context

The bus must handle 50,000 events/s with growth to millions, retain streams for replay such as projection rebuilding and rule dry runs, preserve ordering by key, support read-process-write transactions, and provide replication with RPO = 0. Kafka was selected for these requirements; this ADR defines its distribution, version line and mode.

## Considered options

| Option | Advantages | Disadvantages |
|---|---|---|
| **Upstream Apache Kafka 4.x, KRaft** | Apache 2.0; transactions and EOS; reference compatibility with librdkafka; no ZooKeeper | JVM on brokers; customer-operated infrastructure |
| Confluent Platform | Ready-made tools | Some components use the non-open Confluent Community License, restricting product distribution |
| Redpanda | C++, one binary, Kafka API | Business Source License; transaction semantics have historically differed from Kafka |
| NATS JetStream | Simplicity | No Kafka-style read-process-write transactions; a smaller replay and compacted-topic ecosystem |
| Apache Pulsar | Separate storage and compute | Operational complexity with BookKeeper; no mature C++ client with EOS |

## Decision

Use the upstream Apache Kafka distribution, version line 4.x, in KRaft mode. ZooKeeper is not used and was removed in 4.x. Pin the exact minor version in environment configuration and upgrade under ADR-024.

- Cluster: three combined broker/controller nodes; dedicated controllers with five or more nodes.
- Single machine: one combined broker/controller node.
- Java: an OpenJDK LTS version supported by the selected Kafka version, provided by distribution repositories or the image.
- Client: librdkafka for C/C++, with KIP-848 consumer protocol and transaction support, pinned in the Conan lock.
- Security: SASL/SCRAM or mTLS for services, with topic ACLs; requirements SR-04, SR-08 and SR-19.

## Consequences

- Kafka is required in every topology, including a single machine. This is the deliberate cost of consistent semantics.
- Pin the official Apache Kafka image by version and digest; verify deployment on Debian 13 x86_64.
- Installer automation, disk monitoring and runbooks reduce Kafka's operational burden.

## Verification

Kafka-layer integration tests forcibly terminate brokers; cluster chaos checks exercise failures.
