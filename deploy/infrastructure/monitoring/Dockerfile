ARG BASE
FROM ${BASE}
USER root
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    python3 python3-venv python3-psycopg2 python3-cryptography python3-prometheus-client \
 && rm -rf /var/lib/apt/lists/*
COPY requirements.lock /opt/psim/requirements.lock
RUN python3 -m venv --system-site-packages /opt/monitoring \
 && /opt/monitoring/bin/pip install --disable-pip-version-check --no-cache-dir --require-hashes --only-binary=:all: -r /opt/psim/requirements.lock
COPY agent.py /opt/psim/agent.py
ENV PYTHONDONTWRITEBYTECODE=1 PATH="/opt/monitoring/bin:${PATH}"
USER 60027:60027
ENTRYPOINT ["python3", "-I", "/opt/psim/agent.py"]
