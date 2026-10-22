ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-resource-catalog /usr/bin/psim-resource-catalog
COPY taxonomy.yaml /usr/share/psim/taxonomy.yaml
USER 65532:65532
ENTRYPOINT ["/usr/bin/psim-resource-catalog"]
CMD ["--version"]
