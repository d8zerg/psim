# Documentation portal tooling (step 1.6, ADR-038): MkDocs with Material, pinned versions.
# Built by `task docs:portal:image`; the tag follows this file and requirements.txt.
ARG BASE=python:3.13.9-slim-bookworm@sha256:b685a4fa58bb19d1814d78a1ec0f0208f351452724f78b20212c984d6e124a34
FROM ${BASE}
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir --disable-pip-version-check -r /tmp/requirements.txt && rm /tmp/requirements.txt
# Redoc renderer of the REST reference, served with the portal instead of the CDN (offline portal).
# Its version must match the one referenced by the pinned Redocly CLI (checked by task docs:portal).
ADD --checksum=sha256:dcaf76612bc4a3fbcc923a8966dee2f6146a5f32e5ce1b6f02dd60cbbf89500b \
    https://cdn.redocly.com/redoc/v2.5.4/bundles/redoc.standalone.js /opt/portal/redoc.standalone.js
RUN chmod 644 /opt/portal/redoc.standalone.js
# MkDocs is pinned to 1.6 (ADR-038), so the MkDocs 2.0 notice of Material is not needed.
ENV PYTHONDONTWRITEBYTECODE=1 NO_MKDOCS_2_WARNING=true
WORKDIR /repo
