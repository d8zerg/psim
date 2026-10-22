# PSIM Notification Service runtime image.
ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psim-notification-service /usr/bin/psim-notification-service
USER 65532:65532
EXPOSE 9100
ENTRYPOINT ["/usr/bin/psim-notification-service"]
CMD ["--version"]
