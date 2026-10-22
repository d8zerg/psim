ARG BASE=python:3.13.9-slim-trixie@sha256:326df678c20c78d465db501563f3492d17c42a4afe33a1f2bf5406a1d56b0e86
FROM ${BASE}
RUN pip install --no-cache-dir --disable-pip-version-check openapi-schema-validator==0.6.3 jsonschema==4.23.0 pyyaml==6.0.3
ENV PYTHONDONTWRITEBYTECODE=1
WORKDIR /repo
