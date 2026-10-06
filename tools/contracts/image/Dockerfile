# Contract tooling image: buf with local protoc plugins (ADR-030).
# Local plugins remove the dependency on BSR remote plugins (rate limits, offline CI).
# Built by: task tools:build
ARG BUF_VERSION=1.73.0
ARG GO_VERSION=1.27.1

FROM golang:${GO_VERSION}-alpine AS plugins
ARG GNOSTIC_VERSION=v0.7.0
ARG PROTOC_GEN_DOC_VERSION=v1.5.1
ENV CGO_ENABLED=0 GOBIN=/out
RUN go install github.com/google/gnostic/cmd/protoc-gen-openapi@${GNOSTIC_VERSION} \
 && go install github.com/pseudomuto/protoc-gen-doc/cmd/protoc-gen-doc@${PROTOC_GEN_DOC_VERSION}

FROM bufbuild/buf:${BUF_VERSION}
COPY --from=plugins /out/ /usr/local/bin/
