ARG BASE=postgres:18.6-trixie@sha256:74935e72241653ca55e0414067e6d8763aceb8a810eb51b452253ec3dcfc4336
FROM ${BASE}
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    python3 python3-venv python3-boto3 python3-cryptography python3-psycopg2 \
 && rm -rf /var/lib/apt/lists/*
COPY requirements.lock /opt/operations-test-requirements.lock
RUN python3 -m venv --system-site-packages /opt/operations-test \
 && /opt/operations-test/bin/pip install --disable-pip-version-check --no-cache-dir --require-hashes --only-binary=:all: -r /opt/operations-test-requirements.lock
ENV PYTHONDONTWRITEBYTECODE=1 PATH="/opt/operations-test/bin:${PATH}"
