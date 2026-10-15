ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-incident-service /usr/bin/psim-incident-service
USER 65532:65532
ENTRYPOINT ["/usr/bin/psim-incident-service"]
CMD ["--version"]
