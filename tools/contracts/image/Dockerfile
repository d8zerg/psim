# Contract tooling image: buf with local protoc plugins (ADR-030).
# Local plugins remove the dependency on BSR remote plugins (rate limits, offline CI).
# Built by: task tools:build
# Base images are pinned by digest (SR-23); update tag and digest together.
ARG BUF_IMAGE=bufbuild/buf:1.73.0@sha256:75d8f756f0919dcded71e67968a91adfe55fe4dd62c4900c57bbfcd7fe625608
ARG GO_IMAGE=golang:1.27.1-alpine@sha256:8a5910f31396cd4d89662f56c68b3ae31d374308270a1c3bd96672ee5ed43414

FROM ${GO_IMAGE} AS plugins
ARG GNOSTIC_VERSION=v0.7.0
ARG PROTOC_GEN_DOC_VERSION=v1.5.1
ENV CGO_ENABLED=0 GOBIN=/out
RUN go install github.com/google/gnostic/cmd/protoc-gen-openapi@${GNOSTIC_VERSION} \
 && go install github.com/pseudomuto/protoc-gen-doc/cmd/protoc-gen-doc@${PROTOC_GEN_DOC_VERSION}

FROM ${BUF_IMAGE}
COPY --from=plugins /out/ /usr/local/bin/
