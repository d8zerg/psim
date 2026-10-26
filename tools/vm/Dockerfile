ARG BASE
FROM ${BASE}
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    qemu-system-x86 qemu-utils cloud-image-utils iproute2 iptables dnsmasq-base \
    openssh-client python3 ca-certificates curl tini \
 && rm -rf /var/lib/apt/lists/*
ARG VM_UID=1000
ARG VM_GID=1000
RUN (getent group ${VM_GID} >/dev/null || groupadd --gid ${VM_GID} psim-platform-vm) \
 && useradd --create-home --uid ${VM_UID} --gid ${VM_GID} --shell /bin/sh psim-platform-vm
COPY controller.py prepare.py /opt/psim-platform-vm/
ENTRYPOINT ["/usr/bin/tini", "--", "python3", "/opt/psim-platform-vm/controller.py"]
CMD ["daemon"]
