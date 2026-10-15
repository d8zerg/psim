ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-normalizer /usr/bin/psim-normalizer
USER 65532:65532
ENTRYPOINT ["/usr/bin/psim-normalizer"]
CMD ["--version"]
