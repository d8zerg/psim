# ADR-077. Operational Topology of SeaweedFS

<!-- language-links -->
[English](0077-seaweedfs-deployment-topology.md) | [Русский](../ru/adr/0077-seaweedfs-deployment-topology.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-023, ADR-042, ADR-043, ADR-046

## Context

Floor plans are stored as private immutable objects; versions and links belong to the Resource Catalog. For cluster delivery, replicas need to be placed on different nodes, with shared filer metadata and verifiable recovery after process failure.

## Solution

1. On one machine, use one master, volume, filer, and S3 gateway with `000` placement. Loss of a machine stops file access; recovery depends on a backup.
2. In the cluster, use three masters, three volumes, two filers, and two S3 gateways. Each volume has its own rack; `010` requires two copies in different racks of one data center. The local VM environment simulates three racks in guest OSs on a single physical host.
3. Metadata of the filer are stored in a separate PostgreSQL database, managed by the same Patroni cluster with synchronous replication. Both filers use the same PostgreSQL store. Local filer directories are not independent metadata sources.
4. Separate trust for internal HTTPS/gRPC SeaweedFS and public S3 HTTPS. Internal calls use a separate CA, mTLS, and allowed names lists. S3 checks SigV4 and only grants Catalog permissions to bucket `psim-plans`. Anonymous requests are prohibited.
5. Enable volume data encryption with SeaweedFS. Metadata of the filer with decryption keys is included in the protected backup set. A volume copy without metadata is not a complete plan copy.
6. The Resource Catalog creates a new object key for each version; S3 gateway does not manage object versions. Object cleanup takes into account Catalog links, protection interval, and PITR window according to ADR-046.
7. Recovery after a volume loss includes checking placement and fixing missing replicas via `volume.fix.replication`. Restarting the process alone does not prove recovery of the number of copies.

## Verification

C-15 should stop one volume, filer, and S3 gateway, check current plan reading during failure, recover the process, check loading of the next version and number of replicas. Anonymous reading is checked on an existing object. Consistency of backup with PITR and complete recovery are part of the backup procedure.

Status checks and local environment limits are recorded in [topology deployment](../engineering/deployment-topologies.md) and [release conditions](../product/release-readiness.md).
