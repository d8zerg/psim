ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-event-history /usr/bin/psim-event-history
USER 65532:65532
ENTRYPOINT ["/usr/bin/psim-event-history"]
CMD ["--version"]
