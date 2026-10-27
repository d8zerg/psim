# Documentation portal tooling (ADR-038): MkDocs with Material, pinned versions.
# Built by `task docs:portal:image`; the tag follows this file and requirements.txt.
ARG BASE=python:3.13.9-slim-trixie@sha256:326df678c20c78d465db501563f3492d17c42a4afe33a1f2bf5406a1d56b0e86
FROM ${BASE}
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir --disable-pip-version-check -r /tmp/requirements.txt && rm /tmp/requirements.txt
# Redoc renderer of the REST reference, served with the portal instead of the CDN (offline portal).
ADD --checksum=sha256:dcaf76612bc4a3fbcc923a8966dee2f6146a5f32e5ce1b6f02dd60cbbf89500b \
    https://cdn.redocly.com/redoc/v2.5.4/bundles/redoc.standalone.js /opt/portal/redoc.standalone.js
ADD --checksum=sha256:1dab4315f8de2177b2b2726b29e84a26314c69cd27d692228947c0058793d5a7 \
    https://cdn.redoc.ly/redoc/logo-mini.svg /opt/portal/redoc-logo.svg
RUN sed -i 's#https://cdn.redoc.ly/redoc/logo-mini.svg#redoc-logo.svg#g' /opt/portal/redoc.standalone.js \
    && chmod 644 /opt/portal/redoc.standalone.js /opt/portal/redoc-logo.svg
# MkDocs is pinned to 1.6 (ADR-038), so the MkDocs 2.0 notice of Material is not needed.
ENV PYTHONDONTWRITEBYTECODE=1 NO_MKDOCS_2_WARNING=true
WORKDIR /repo
