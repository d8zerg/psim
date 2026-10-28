# Delivering two deployment topologies

<!-- language-links -->
[English](deployment-topologies.md) | [Русский](../ru/engineering/deployment-topologies.md)
<!-- /language-links -->

Status: Accepted. Single-machine and cluster deployments use the same services and contracts. Their inventories, infrastructure configuration and instance counts differ. Measured local-profile performance is confirmed only within the verification boundaries described below.

## Profiles

| Profile | Purpose |
|---|---|
| `deploy/topologies/single.json` | One Debian 13 x86_64 machine |
| `deploy/topologies/cluster.json` | Separate infrastructure, application and observability nodes |
| `deploy/topologies/local-single.json` | Single-machine topology in one local VM |
| `deploy/topologies/local-vm.json` | Cluster on three local VMs |

Profiles specify addresses, failure domains, public address, VIP and VRRP interface. The local cluster allocates three VMs, each with 4 vCPU, 12 GiB RAM and a 60 GiB disk. One physical machine can verify guest shutdown and process failover; it cannot establish independence of physical failure domains. The laboratory is described in the [VM guide](local-vm-lab.md).

## Starting the local deployment

Run commands from the repository root on Linux with native Docker Engine, Compose and available KVM.

```sh
task deploy:images
task deploy:vm:up TOPOLOGY=cluster
task deploy:vm:status
task deploy:vm:down
task deploy:vm:up TOPOLOGY=cluster
```

`up` creates the public configuration bundle, generates fresh laboratory PKI and account material, starts VMs, installs pinned Docker packages, transfers configuration and images, and configures infrastructure, Kafka topics and ACLs, Registry schemas and the private bucket. The command completes only when every application instance is ready.

Registry, Keycloak, filer and S3 start after checking that the PostgreSQL primary is writable and synchronous replicas are available. This database check precedes their startup both during initial installation and when resuming a stopped laboratory. Registry cannot initialize before PostgreSQL is ready.

The console is available at `https://localhost:8443`, and Connector Gateway at `localhost:7443`. The public CA used to verify HTTPS is `build/deployment/lab-cluster-ca.pem`. CA private keys remain on the management machine; each VM receives only material for its own roles.

Destroy the current laboratory before changing topology:

```sh
task deploy:vm:destroy
task deploy:vm:up TOPOLOGY=single
```

`down` preserves disks and identifiers. `destroy` deletes the current laboratory's own VMs, disks, private material, node archives and public bundle. Shared base-image and package caches are retained. Unrelated containers and directories are not deleted. Changes to the inventory, public bundle or image identifiers require recreation; `up` does not replace an existing installation with a different bundle.

Laboratory certificates are valid for two days. Laboratory issuance is separate from production external PKI. Long-term operation requires an external issuance and renewal process.

Once the deployment is ready, create the initial administrator:

```sh
task deploy:account BUNDLE=build/deployment/lab-cluster PRIVATE=.cache/deployment/lab-cluster/material USERNAME=platform-admin
```

The command prompts for a temporary password. First login requires changing it and enrolling TOTP. External issuance, initial trust, registration and renewal are described in the [PKI and Keycloak procedure](deployment-pki.md).

## Application runtime modes

Infrastructure uses Docker Compose in both topologies. Applications run in containers or from Debian packages under systemd, retaining configuration, UIDs, secrets, ports and data directories.

```sh
task release:package
task deploy:runtime RUNTIME=systemd
task deploy:flow
task deploy:runtime RUNTIME=containers
```

`psim-taxonomy` supplies the shared taxonomy file; consumer packages depend on the same version. Switching stops the previous runtime mode and requires the new mode to become ready. Use the VM factory to remove the entire local laboratory. Running infrastructure without containers is outside this delivery package.

## Repeatable checks

| Command | Verified property |
|---|---|
| `task vm:test` | KVM lifecycle, resource isolation, shutdown and deletion of owned resources |
| `task deploy:check` | Four profiles, Compose, Helm, UIDs and isolation of instance material |
| `task deploy:flow` | Fresh connector, floor plan, normalized history, incident and shared console |
| `task deploy:gateway:bench` | Ingest 400,000 events through two gateways with a 50,000 events/s gate and Kafka reconciliation; other applications are temporarily stopped |
| `task deploy:gateway:failover` | Active-process loss, SDK failover and reconciliation of 1000 events |
| `task deploy:postgres:qualify` | Shutdown of the primary VM, write RTO and RPO of acknowledged transactions |
| `task deploy:console:qualify` | Writer suspension, ownership transfer and fencing of the resumed process |
| `task deploy:storage:qualify` | Floor-plan reads through seven process failures, recovery, a new version and rejection of anonymous reads |
| `task deploy:operators:qualify` | SHA-256 verification and rendering of pinned operators |

Gateway load tests and writer suspension use containerized applications. Storage checks use containerized infrastructure with either application runtime mode. Checks use their own installation and fresh credentials; their operations are serialized with `up`, `down`, `destroy` and runtime switching.

## Role isolation

Each application and infrastructure role receives a separate numeric UID, a `0700` material directory and `0400` files. Images run without root, with a read-only root filesystem and no capabilities. HAProxy receives only permission to bind its port; VRRP runs as a separate systemd process with restricted network capabilities. The PostgreSQL hardware watchdog is accessible only to its role.

Kafka uses mTLS and explicit ACLs for topics, groups and transactional identifiers; automatic topic creation is disabled. PostgreSQL grants application roles their own schemas without permission to create databases or roles. ClickHouse loader journals are separated by RLS policies. ClickHouse separates writer and reader roles; Valkey separates application ACLs, replication and Sentinel administration. Registry prevents application clients from modifying schemas. S3 grants Catalog access only to the floor-plan bucket and prohibits anonymous access.

Keycloak issues tokens with audience `psim` and resource-server audiences. Each service performs introspection using its own credentials. Audience validation remains enabled during introspection. Secrets, tokens and keys are not passed as command arguments or committed to Git.

## Verification scope

Verified behavior includes VM creation and deletion, readiness of all 26 cluster application instances, and public HTTPS with CA verification and TLS 1.3. The two-gateway ingestion check measured 296,905 events/s: 400,000 acknowledged events in 1.347 seconds, all found in Kafka. Other application services were stopped for this run. The measurement boundary was gateway batch receipt -> acknowledgment by every Kafka ISR. Client preparation and transport shutdown were excluded. Services were restored afterward, with readiness required for all 26 instances. This run does not establish 10-minute stability of the entire domain pipeline.

Two runs with background applications running and only normalizers stopped achieved 32,850 and 38,100 events/s, with complete Kafka reconciliation. Both are below 50,000 events/s. On the shared local profile, L-06 separately checks gateway -> Kafka ingestion. Combined application throughput and sustained load require separate qualification on the reference configuration.

L-06 synthetic load has no domain source catalog and is processed through DLQ when normalizers resume. The accumulated queue on the shared profile can delay the next domain event: one end-to-end run after load exceeded its 90-second limit. Wait for the queue to drain before a separate functional check. That limit is not evidence of pipeline latency under load. Registration in the qualification client retries only transient errors while preserving the request identifier.

When the active gateway was forcibly terminated with automatic restart disabled, the SDK switched to another replica: 1000 acknowledgments, zero rejections and zero pending events; Kafka reconciliation found no losses. When the PostgreSQL primary VM was shut down, writes resumed after 31.54 seconds and all 11 acknowledged records were retained. Shared-console writer failover after process suspension took 17.522 seconds; the resumed process relinquished ownership.

The end-to-end domain scenario passed on the cluster in containers and under systemd. Sequential shutdown of three volumes, two filers and two S3 gateways preserved the floor plans' original SHA-256 values. After recovery, checks confirmed at least two copies of each of 12 volumes in different racks, upload of the next version, and HTTP 403 for an anonymous request to an existing object. This is the installation's minimum file-storage check. Extended C-15 includes additional failures and writes during degraded availability.

Verification covered 300 unit tests and 12 runtime tests under ASan/UBSan and TSan. A fatal transactional-producer error terminates the process for supervisor restart; actual normalizer fencing was checked on the cluster. Initial external PKI completed CSR -> external signature -> verification -> node archive. Account creation was checked with mandatory password change and TOTP enrollment.

A clean single-machine installation passed the end-to-end scenario in containers and under systemd. Checks covered shutdown and resume with packages and data preserved, return to containers, and another end-to-end run. A recreated cluster passed ingestion, failure, repeated `up`, shutdown, cold-resume and final end-to-end checks. A previously created floor plan retained its checksum. The installer sets permissions for both watchdog interfaces; a udev rule preserves ownership after reboot. Reports are `build/deployment/canonical-qualification.json` and `build/deployment/watchdog-lifecycle.json`. Confirmed-result boundaries and release conditions are recorded in [release readiness](../product/release-readiness.md).

## Kubernetes

The [Helm chart](../../deploy/helm/psim-platform/Chart.yaml) includes the backend, web client and public HAProxy. It accepts file configuration and a separate Secret per instance. An init container copies the projected Secret into user-owned `0400` files in memory. Processes run without root or privilege escalation. Health checks, graceful `SIGTERM`, persistent volumes, replica placement on different nodes and a PodDisruptionBudget are provided. Only the public proxy may bind a privileged port. Images are specified by digest.

`tools/deploy/helm.py` generates values from the public bundle and local OCI images. Before installation, supply target DNS names and issuer, infrastructure addresses, external Secrets and a suitable storage class. External changes to a Secret require restarting the corresponding StatefulSet so that its init container prepares fresh material. Static validation uses temporary dummy digests only for rendering; they are removed afterward.

The [operator catalog](../../deploy/operators/operators.json) pins chart versions and SHA-256 values for Strimzi, Zalando Postgres Operator, Altinity ClickHouse Operator and SeaweedFS Operator. Downloads were checksum-verified and their resources rendered. The PostgreSQL operator uses Patroni; its Kubernetes DCS parameters differ from etcd in VM deployments. Infrastructure addresses, PKI, ACLs and secrets require configuration for the target environment.

Chart validation does not include a live Kubernetes installation, storage failover or infrastructure qualification. No custom PSIM operator is provided.
