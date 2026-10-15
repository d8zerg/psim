ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-connector-gateway /usr/bin/psim-connector-gateway
USER 65532:65532
ENTRYPOINT ["/usr/bin/psim-connector-gateway"]
CMD ["--version"]
