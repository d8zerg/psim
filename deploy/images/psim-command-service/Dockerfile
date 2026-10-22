# PSIM Command Service runtime image.
ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-command-service /usr/bin/psim-command-service
USER 65532:65532
EXPOSE 9100
ENTRYPOINT ["/usr/bin/psim-command-service"]
CMD ["--version"]
