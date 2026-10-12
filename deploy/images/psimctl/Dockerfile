# psimctl container image (step 1.3, ADR-034, SR-24): Debian 13 distroless base with the C++ runtime (no shell),
# non-root user, the binary from the release preset. Built by `task release:images` from a staging
# directory that holds this file and the binary; the base digest is pinned in the root Taskfile.
ARG BASE=gcr.io/distroless/cc-debian13:nonroot
FROM ${BASE}
COPY psimctl /usr/bin/psimctl
USER 65532:65532
ENTRYPOINT ["/usr/bin/psimctl"]
CMD ["version"]
