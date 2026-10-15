ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-correlation-engine /usr/bin/psim-correlation-engine
USER 65532:65532
ENTRYPOINT ["/usr/bin/psim-correlation-engine"]
CMD ["--version"]
