# PSIM Audit Service runtime image.
ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-audit-service /usr/bin/psim-audit-service
USER 65532:65532
EXPOSE 9100
ENTRYPOINT ["/usr/bin/psim-audit-service"]
CMD ["--version"]
