ARG BASE
FROM ${BASE}
USER root
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libcap2-bin \
 && setcap cap_net_bind_service=+ep /usr/local/sbin/haproxy \
 && rm -rf /var/lib/apt/lists/*
USER 60023:60023
ENTRYPOINT ["/usr/local/sbin/haproxy"]
