ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-api-gateway /usr/bin/psim-api-gateway
USER 65532:65532
ENTRYPOINT ["/usr/bin/psim-api-gateway"]
CMD ["--version"]
