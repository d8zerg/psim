ARG BASE
FROM ${BASE}
RUN /opt/keycloak/bin/kc.sh build --db=postgres --health-enabled=true --metrics-enabled=true --http-relative-path=/identity
