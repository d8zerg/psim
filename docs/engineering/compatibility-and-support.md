# Compatibility and Support for Versions

<!-- language-links -->
[English](compatibility-and-support.md) | [Русский](../ru/engineering/compatibility-and-support.md)
<!-- /language-links -->

Project Policy - Accepted

## 1. Project Status and Supported Platforms

The Open-Source PSIM Platform is in the MVP stage of development. Implemented functions and limitations are available in [release readiness](../product/release-readiness.md); planned functions are not considered available in release until they are implemented and tested.

The only officially supported OS for the MVP is Debian 13 x86_64. Supported topologies include single machine and cluster on VM; artifact formats include DEB and container images. Users can choose between local or cloud infrastructure.

The project supports the latest released version: corrections are accepted in the main branch and released in the next version. A fixed LTS period and separate backports for branches have not been established yet. Community support does not include guaranteed response time or SLA.

## 2. Versions

- Versions use `MAJOR.MINOR.PATCH`. Changes before 1.0 are listed in release notes; experimental functions are explicitly marked.
- After 1.0, incompatible changes to the public interface require a new major version. New compatible functions are minor; corrections to compatible behavior are patches.
- The contract version is separate from the product version: Protobuf packages `psim.*.v1`, REST `/v1`, and topic suffixes `.v1` remain when compatible extensions are added.
- Before updating, the operator reads release notes: migrations, update order, configuration changes, support for old connectors, and rollback conditions.

## 3. Rules for Extending Contracts

1. Existing Protobuf field numbers are not reused; deleted numbers and names are reserved. The meaning of fields and enum values remains unchanged.
2. New fields have safe behavior when not present. It is not allowed to require a new parameter from an existing client in the old API version.
3. The new variant `oneof`, enum values, or realtime deltas are tested against the old client. Subscribing to a new stream or type requires explicit support from the client; unknown types are not sent to the old client.
4. Topic schemas are checked for compatibility relative to the latest released version; tools include [contracts:check](../../contracts/README.md) and `contracts:registry-compat`.
5. Deleting or changing the meaning of an API is first announced in notes and documentation with a migration path. For incompatible contracts, a new version is introduced; existing consumers are not automatically switched to it.
6. Tests include the old client with the new server and the new client with the supported old server; non-supported features are explicitly rejected during negotiation.

Connector Gateway maintains support for the current and previous major versions of the protocol according to [ADR-017](../adr/0017-connector-protocol.md). The start and end of support for a version are listed in release notes. The presence of proto files does not mean that the version is supported by the running server.

## 4. Data, Updates, and Rollbacks

Migrations are performed using expand/contract from [ADR-024](../adr/0024-zero-downtime-upgrades.md). Supported N -> N+1 pairs and rollback windows are checked during update qualification. Each release describes compatibility with schemas, configurations, state formats, and file objects; rollbacks are not available without checking this pair.

Backups include PostgreSQL, ClickHouse data outside Kafka retention, configuration and compacted topics, Keycloak, trust material, and file objects. Before restoration, versions, integrity, and key rotation conditions are checked.

## 5. Publishing

Each release contains notes with implemented functions and known limitations, signed artifacts, SBOM, and instructions for verifying signatures. Risk-bearing changes and required operator actions are described before the update instruction. Releases are published in [GitHub Releases](https://github.com/d8zerg/psim/releases).
