# ADR-003. Schema Registry

<!-- language-links -->
[English](0003-schema-registry.md) | [Русский](../ru/adr/0003-schema-registry.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-004, ADR-005

## Context

Topic-message schemas must be versioned, checked for compatibility during registration, and available to external consumers such as integrators and future AI modules for decoding. An open-source implementation with a Confluent-compatible API is required because that API is the de facto standard for tooling and message framing.

## Considered options

| Option | Advantages | Disadvantages |
|---|---|---|
| **Apicurio Registry 3.x** | Apache 2.0; Confluent-compatible API (`ccompat/v7`); mature Protobuf dependency support; compatibility rules; PostgreSQL or Kafka storage; multiple stateless instances | JVM; another service |
| Karapace | Apache 2.0; drop-in Confluent SR replacement; Kafka storage | Python; Protobuf import support developed later than Avro support |
| Confluent Schema Registry | Reference API implementation | Non-open Confluent Community License |
| No registry: schemas only in the repository, type in a header | No infrastructure | No runtime compatibility checks or central source for external consumers; no centralized schema control |

## Decision

Use Apicurio Registry 3.x:

- Store data in a separate `registry` database on platform PostgreSQL, using the existing PostgreSQL cluster for high availability.
- Run two stateless instances in a cluster and one on a single machine.
- Clients use the Confluent-compatible API and message framing from ADR-004.
- The installer and CI set per-subject compatibility rules during schema registration.
- Only the installer and CI register schemas, using a write-authorized account. Services read through a cache and never register schemas automatically.

## Consequences

- No ready-made C++ client is available. The platform library implements a small client for schema lookup by ID, ID lookup by subject and version, and caching.
- Services do not depend on the registry on the event-processing path. Schema IDs are known and cached at startup; registry unavailability does not stop processing.
- Karapace is an API-compatible fallback; changing the implementation does not affect services.

## Verification

A CI contract test registers current-branch schemas with compatibility rules against the latest release's schemas.
