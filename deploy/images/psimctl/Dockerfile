# Administrative client tools include PostgreSQL physical-backup utilities.
ARG OPERATIONS_BASE
FROM ${OPERATIONS_BASE}
COPY psimctl /usr/bin/psimctl
COPY psim-ch-migrate psim-pg-migrate psim-gateway-admin psim-audit-tool psim-console-tool psim-report-tool psim-response-replay /usr/bin/
USER 65532:65532
ENTRYPOINT ["/usr/bin/psimctl"]
CMD ["version"]
