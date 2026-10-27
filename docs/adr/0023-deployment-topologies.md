# ADR-023. Deployment topologies and single-machine reliability limits

<!-- language-links -->
[English](0023-deployment-topologies.md) | [Русский](../ru/adr/0023-deployment-topologies.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-002, ADR-015, ADR-024, [deployment.md](../architecture/deployment.md)

## Context

One distribution must support a single machine through a cluster; Kubernetes is outside MVP scope. The cluster must achieve RTO ≤ 60 s and RPO = 0 after one node fails. Users choose local or cloud infrastructure; Debian 13 x86_64 is officially supported. Users have different levels of operational experience.

## Options considered

### PostgreSQL high availability

| Option | Advantages | Disadvantages |
|---|---|---|
| **Patroni + etcd** | De facto standard; synchronous replication and automatic failover; mature documentation | Two additional components: Patroni and etcd |
| pg_auto_failover | Simpler | Monitor node is a single point of failure; less widely used |
| Manual failover | No additional components | Cannot meet RTO ≤ 60 s |

### Service execution

| Option | Advantages | Disadvantages |
|---|---|---|
| **Docker Compose infrastructure; application containers or `.deb` packages under systemd** | Shared infrastructure and application configuration; two process-launch formats | Both application formats require end-to-end tests |
| Containers only | One format | No native package installation under systemd on Debian 13 x86_64 |
| Kubernetes | Orchestration | Outside MVP (X15) |

## Decision

1. **Single machine:** all components on one server, with one Kafka node and explicit [reliability limits](../architecture/deployment.md#2-one-machine-topology): server-failure RTO lasts until server recovery; disk-loss RPO equals the backup interval.
2. **VM cluster:** 3 Kafka broker/controller nodes, RF 3, `min.insync.replicas=2`; PostgreSQL with Patroni, etcd and one synchronous replica; the ADR-015 Redis-compatible store; HAProxy ×2 and keepalived; at least 2 instances of every service on separate nodes.
3. Infrastructure uses native Docker Engine and Compose on Debian 13 x86_64. Applications support containers and `.deb` packages under systemd. Both formats undergo end-to-end testing with the same ports, UIDs, configuration files, PKI and data directories.
4. The installer generates configuration from a topology profile; topologies differ only in configuration and instance counts.
5. Local E-CLUSTER uses three KVM guests, each with 4 vCPU, 12 GiB RAM and 60 GiB disk, on one Linux host. It tests guest-OS and process failure. Qualifying independent physical failure domains requires separate hosts.
6. L-06 on the colocated local profile separately tests input through two gateways up to Kafka acknowledgement. Other application processes are temporarily stopped, then restored with readiness checks. The threshold remains 50,000 events/s. Combined service performance and sustained full-pipeline load are qualified separately on the reference configuration; local input measurements do not prove them.

## Consequences

- The distribution includes Patroni, etcd, HAProxy and keepalived, with runbooks and monitoring.
- The Helm chart contains backend services, web client and public proxy; infrastructure uses operators from a pinned catalog. Checks cover rendering, image digests, file-material isolation, non-root execution, health and `SIGTERM`. Live Kubernetes installation and full qualification are outside MVP.
- A single-machine deployment stops serving when that machine is lost. A cluster on one physical host retains the same shared failure boundary.
- External PKI and initial-account procedures are included in the installation package.

## Verification

Install each topology using its documentation, run an end-to-end test and execute node-failure chaos scenario S11.

Commands, results and limitations: [topology delivery](../engineering/deployment-topologies.md), [KVM environment](../engineering/local-vm-lab.md), [trust and accounts](../engineering/deployment-pki.md). File-storage topology: [ADR-077](0077-seaweedfs-deployment-topology.md).
