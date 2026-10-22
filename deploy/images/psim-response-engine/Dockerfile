# PSIM Response Engine runtime image.
ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-response-engine /usr/bin/psim-response-engine
USER 65532:65532
EXPOSE 9100
ENTRYPOINT ["/usr/bin/psim-response-engine"]
CMD ["--version"]
