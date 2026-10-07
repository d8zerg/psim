#include "psim/platform/kafka/trace_context.hpp"

#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/span_id.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <opentelemetry/trace/trace_flags.h>
#include <opentelemetry/trace/trace_id.h>
#include <opentelemetry/trace/trace_state.h>
#include <opentelemetry/trace/tracer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "psim/platform/kafka/client.hpp"

namespace psim::platform::kafka {

namespace otel = opentelemetry;

namespace {

// traceparent: "00-<trace id, 32 hex>-<span id, 16 hex>-<flags, 2 hex>" (W3C Trace Context, level 1).
constexpr std::string_view kTraceParent = "traceparent";
constexpr std::string_view kTraceState = "tracestate";
constexpr std::size_t kTraceIdHex = 32;
constexpr std::size_t kSpanIdHex = 16;
constexpr std::size_t kTraceParentSize = 2 + 1 + kTraceIdHex + 1 + kSpanIdHex + 1 + 2;

std::optional<std::uint8_t> nibble(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<std::uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<std::uint8_t>(c - 'a' + 10);
  }
  return std::nullopt;
}

// Lower-case hex into bytes; false on any other character.
template <std::size_t N>
bool parse_hex(std::string_view hex, std::array<std::uint8_t, N>& out) {
  for (std::size_t i = 0; i < N; ++i) {
    const auto high = nibble(hex[2 * i]);
    const auto low = nibble(hex[(2 * i) + 1]);
    if (!high || !low) {
      return false;
    }
    out.at(i) = static_cast<std::uint8_t>((*high << 4U) | *low);
  }
  return std::ranges::any_of(out, [](std::uint8_t byte) { return byte != 0; });  // all zeros is invalid
}

void set_header(Headers& headers, std::string_view name, std::string value) {
  std::erase_if(headers, [&](const auto& header) { return header.first == name; });
  headers.emplace_back(std::string(name), std::move(value));
}

}  // namespace

void inject(const otel::trace::Span& span, Headers& headers) {
  const auto context = span.GetContext();
  if (!context.IsValid()) {
    return;
  }
  std::array<char, kTraceIdHex> trace_id{};
  std::array<char, kSpanIdHex> span_id{};
  context.trace_id().ToLowerBase16(trace_id);
  context.span_id().ToLowerBase16(span_id);
  std::string parent = "00-";
  parent.append(trace_id.data(), trace_id.size());
  parent += '-';
  parent.append(span_id.data(), span_id.size());
  parent += context.IsSampled() ? "-01" : "-00";
  set_header(headers, kTraceParent, std::move(parent));
  if (const auto state = context.trace_state()->ToHeader(); !state.empty()) {
    set_header(headers, kTraceState, state);
  }
}

otel::trace::SpanContext extract(const Message& message) {
  const auto parent = message.header(kTraceParent);
  if (!parent || parent->size() != kTraceParentSize || !parent->starts_with("00-") || (*parent)[35] != '-'
      || (*parent)[52] != '-') {
    return otel::trace::SpanContext::GetInvalid();
  }
  std::array<std::uint8_t, kTraceIdHex / 2> trace_id{};
  std::array<std::uint8_t, kSpanIdHex / 2> span_id{};
  std::array<std::uint8_t, 1> flags{};
  if (!parse_hex(parent->substr(3, kTraceIdHex), trace_id) || !parse_hex(parent->substr(36, kSpanIdHex), span_id)) {
    return otel::trace::SpanContext::GetInvalid();
  }
  std::ignore = parse_hex(parent->substr(53, 2), flags);  // zero flags are valid: not sampled
  const auto state = message.header(kTraceState).value_or(std::string_view{});
  using TraceBytes = otel::nostd::span<const std::uint8_t, kTraceIdHex / 2>;
  using SpanBytes = otel::nostd::span<const std::uint8_t, kSpanIdHex / 2>;
  return {otel::trace::TraceId(TraceBytes(trace_id.data(), trace_id.size())),
          otel::trace::SpanId(SpanBytes(span_id.data(), span_id.size())), otel::trace::TraceFlags(flags[0]), true,
          otel::trace::TraceState::FromHeader(otel::nostd::string_view(state.data(), state.size()))};
}

otel::nostd::shared_ptr<otel::trace::Span> start_consumer_span(otel::trace::Tracer& tracer, std::string_view name,
                                                               const Message& message) {
  otel::trace::StartSpanOptions options;
  options.kind = otel::trace::SpanKind::kConsumer;
  options.parent = extract(message);
  const std::string_view topic = message.topic();
  return tracer.StartSpan(otel::nostd::string_view(name.data(), name.size()),
                          {{"messaging.system", "kafka"},
                           {"messaging.destination.name", otel::nostd::string_view(topic.data(), topic.size())},
                           {"messaging.destination.partition.id", static_cast<std::int64_t>(message.partition())},
                           {"messaging.kafka.offset", message.offset()}},
                          options);
}

}  // namespace psim::platform::kafka
