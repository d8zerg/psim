ARG BASE
FROM ${BASE}
USER root
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    python3 python3-venv libnss-wrapper \
 && rm -rf /var/lib/apt/lists/*
COPY requirements.lock build-requirements.lock /opt/psim/
RUN python3 -m venv /opt/patroni \
 && /opt/patroni/bin/pip install --disable-pip-version-check --require-hashes --only-binary=:all: -r /opt/psim/build-requirements.lock \
 && /opt/patroni/bin/pip install --disable-pip-version-check --require-hashes --no-build-isolation -r /opt/psim/requirements.lock \
 && rm -rf /root/.cache/pip
COPY psim-postgres-single /usr/local/bin/psim-postgres-single
COPY psim-audit-tool /usr/bin/psim-audit-tool
COPY psimctl /usr/bin/psimctl
COPY pg-env.sh /opt/psim/pg-env.sh
COPY psim-postgres-env psim-postgres-bin /usr/local/bin/
RUN chmod 0555 /usr/local/bin/psim-postgres-single /usr/local/bin/psim-postgres-env /usr/local/bin/psim-postgres-bin \
 && mkdir /opt/psim/postgres-bin \
 && for binary in /usr/lib/postgresql/18/bin/*; do ln -s /usr/local/bin/psim-postgres-bin "/opt/psim/postgres-bin/$(basename "$binary")"; done
ENV PATH="/opt/patroni/bin:${PATH}" \
    PGDATA=/var/lib/postgresql/data \
    LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libnss_wrapper.so \
    NSS_WRAPPER_PASSWD=/etc/psim-platform/postgres/secrets/passwd \
    NSS_WRAPPER_GROUP=/etc/psim-platform/postgres/secrets/group \
    PYTHONDONTWRITEBYTECODE=1
USER 60013:60013
ENTRYPOINT []
CMD ["postgres", "--version"]
