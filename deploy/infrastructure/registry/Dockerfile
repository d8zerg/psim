ARG BASE
FROM ${BASE}
RUN QUARKUS_HTTP_SSL_CLIENT_AUTH=required \
    QUARKUS_ARC_EXCLUDE_TYPES=io.apicurio.registry.auth.AppAuthenticationMechanism \
    /deployments/build.sh --prune
