# Local Test Environment on VMs

<!-- language-links -->
[English](local-vm-lab.md) | [Русский](../ru/engineering/local-vm-lab.md)
<!-- /language-links -->

Status: Accepted. The test environment is used for developing cluster services. A single physical machine cannot replicate independent physical failure domains and cannot replace the reference cluster for performance validation.

## Resources

The [local.json](../../deploy/vm/local.json) profile creates three Debian 13 x86_64 VMs. Each VM is allocated 4 vCPU, 12288 MiB of RAM, and 60 GiB of disk space. In total, 12 vCPU, 36 GiB of RAM, and 180 GiB of logical disk space are required. The disks use copy-on-write; the initial physical size is significantly smaller than the logical size.

Tested host: AMD Ryzen 9 3900X, 24 logical CPUs, 64 GB RAM, NVMe. Additional 8 GiB of available memory and 20 GiB of free disk space are required before startup. Reservations from other running test environments are taken into account. KVM is mandatory; software emulation is not used.

## Management

Commands are executed from the repository root on Linux with native Docker Engine and `/dev/kvm` and `/dev/net/tun` available.

```sh
task vm:doctor
task vm:up
task vm:status
task vm:exec NODE=node-1 CMD='uname -r'
task vm:bootstrap
task vm:down
task vm:up
task vm:destroy
```

`up` creates or restarts VMs, checks the completion of cloud-init, and writes initial records to disks. A repeated `up` keeps running VMs. `bootstrap` installs Docker Engine and Compose packages with version and SHA-256 checks in guest OSs. Platform services are installed separately.

`down` terminates guest OSs and saves disks, SSH keys, and identifiers. `destroy` deletes control containers and only the directory of the specified test environment. The base image cache and packages are saved. Changing resources, addresses, or bootstrap requires explicit re-building of the environment. Port publishing can be changed via `down/up` with disk preservation.

```sh
task vm:node:kill NODE=node-1
task vm:node:start NODE=node-1
task vm:wait
```

`node:kill` simulates power loss in the selected VM. Only data flushed to disk by the guest OS is saved to disk. Other nodes continue to run.

## Network and Access

VMs are connected via a bridge in the control container network: `10.91.0.11`, `10.91.0.12`, `10.91.0.13`, gateway, and DNS `10.91.0.1`. SSH is published only on `127.0.0.1:2211`, `:2212`, `:2213`. Server key verification is required; expected keys are specified before the first connection. Password entry and root access are prohibited. User `psim` executes administrative commands via sudo within its VM.

QEMU and SSH tools operate from the container. QEMU uses the host user UID. The container gets KVM, TUN, and necessary rights for its own network and managing child processes; the host network and Docker sockets are not transmitted to it.

State is stored in `.cache/vm/psim-platform-vm`, and the directory is in `0700` mode. Private keys and NoCloud images are not added to Git or included in diagnostic reports. Do not display `user-data` and private keys: NoCloud contains server SSH key.

## Testing

```sh
task vm:test
```

Testing uses a separate name `psim-platform-vm-test` and SSH ports `2311-2313`; it requires free resource budget for three VMs. It does not use an existing environment with such a name.

Seven module-level tests of profile and ownership boundaries have been verified: incorrect resources and addresses, repeated nodes, replacing the directory with a symbolic link, foreign UID or path, foreign container, corrupted image download, Docker request failure, and interrupted state recording.

The native test confirmed three independent KVM guest kernels, specified CPU/RAM/disks, repeated `up` without rebooting, TCP between nodes, port publishing on localhost, hardware watchdog, availability of two nodes after the third is shut down, preservation of flushed data after emergency and normal reboots, and deletion of the own environment. Report: `build/vm/qualification.json`. PostgreSQL, Kafka, SeaweedFS, and application pipeline are not confirmed by this test.
