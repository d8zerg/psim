ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-projection-service psim-report-tool psim-console-tool /usr/bin/
USER 65532:65532
ENTRYPOINT ["/usr/bin/psim-projection-service"]
CMD ["--version"]
