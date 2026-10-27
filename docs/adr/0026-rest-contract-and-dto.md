# ADR-026. REST contracts and DTOs: proto-first OpenAPI generation

<!-- language-links -->
[English](0026-rest-contract-and-dto.md) | [Русский](../ru/adr/0026-rest-contract-and-dto.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-004, [crosscutting.md](../architecture/crosscutting.md#14-synchronous-api-between-web-client-and-services)

## Context

OpenAPI describes REST API v1 and generates the web API client. C++ services need JSON DTO serialization without handwritten code for every type. C++ OpenAPI server generators such as cpp-restsdk, pistache and qt do not fit Asio/Beast. Kafka and gRPC contracts already use Protobuf.

## Options considered

| Option | Advantages | Disadvantages |
|---|---|---|
| OpenAPI-first, handwritten C++ DTOs and Boost.JSON | Full OpenAPI control | Manual code and synchronization across roughly 100 endpoints; only tests detect drift |
| OpenAPI-first, C++ DTOs and reflection libraries such as glaze or reflect-cpp | Less handwritten serialization | Structures still manually duplicate OpenAPI; a second IDL alongside Protobuf |
| TypeSpec as the source for OpenAPI and Protobuf | Readable source | Limited Protobuf emitter; another tool and language |
| **Proto-first: Protobuf REST DTOs and methods with `google.api.http` annotations; generated OpenAPI 3; canonical Protobuf JSON mapping** | One IDL for all contracts; generated C++ DTOs and serialization (`JsonStringToMessage`/`MessageToJsonString`); `buf breaking` also checks REST; OpenAPI remains the published web-client contract | Annotations cannot express headers such as ETag and `Idempotency-Key` or the error format, requiring an overlay; Protobuf JSON uses string int64 values and camelCase |

## Decision

1. Define REST resources in `contracts/proto/psim/api/v1/*.proto` using `google.api.http` annotations and `buf validate` field constraints.
2. CI generates OpenAPI 3 with the protoc plugin `protoc-gen-openapi`, pinned in the tools container. Then it applies `contracts/openapi/overlay.yaml` using the OpenAPI Overlay specification: security schemes, `ETag`/`If-Match`/`Idempotency-Key` headers, `application/problem+json` responses (RFC 9457) and cursor-pagination parameters. Publish the result as the REST v1 contract.
3. JSON follows canonical Protobuf mapping: `lowerCamelCase` fields, string enums, RFC 3339 `Timestamp` values and string int64 values. Reject unknown input fields.
4. Generate the web TypeScript client from the final OpenAPI document.
5. Errors use `psim.api.v1.Problem`, with RFC 9457 fields and a catalog `code`.

## Consequences

- REST changes begin in `.proto`; OpenAPI and clients regenerate automatically, preventing drift.
- Protobuf JSON performance is sufficient for hundreds of REST consoles; realtime hot paths use the same messages.
- The overlay is a separate maintained artifact; CI verifies its application.

## Verification

API Gateway contract tests use the final OpenAPI document. CI verifies that committed generated OpenAPI matches the proto sources.
