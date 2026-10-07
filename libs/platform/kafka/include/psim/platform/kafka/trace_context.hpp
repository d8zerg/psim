#pragma once

#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/tracer.h>

#include <string_view>

#include "psim/platform/kafka/client.hpp"

namespace psim::platform::kafka {

/// W3C trace context in the Kafka headers traceparent and tracestate (ADR-005, crosscutting.md 6.1).

/// Write the context of `span` into the headers of an outgoing record; an invalid span writes nothing.
void inject(const opentelemetry::trace::Span& span, Headers& headers);

/// The remote parent carried by a consumed message; invalid when it has none or a malformed one.
[[nodiscard]] opentelemetry::trace::SpanContext extract(const Message& message);

/// Consumer span of a message: kind CONSUMER, the producer's span as parent, messaging attributes.
/// Spans are not kept active across co_await (ADR-016): pass the span on explicitly.
[[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> start_consumer_span(
    opentelemetry::trace::Tracer& tracer, std::string_view name, const Message& message);

}  // namespace psim::platform::kafka
