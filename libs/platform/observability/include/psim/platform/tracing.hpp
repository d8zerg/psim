#pragma once

#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/trace/tracer.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "psim/platform/log.hpp"

namespace psim::platform::observability {

struct TracingOptions {
  std::string service;
  std::string version;
  std::string instance;
  /// OTLP gRPC endpoint of the Collector, for example "otel-collector:4317"; empty disables export.
  std::string otlp_endpoint;
  /// Head sampling of new traces; a sampled parent is always followed (crosscutting.md 6.1).
  double sampling_ratio = 0.01;
};

/// Process-wide tracer provider: OTLP gRPC export in batches, resource attributes of the service.
/// Installed as the global provider for its lifetime; destruction flushes pending spans.
class Tracing {
 public:
  explicit Tracing(const TracingOptions& options);
  /// Spans go to `exporter` (tests use the in-memory exporter).
  Tracing(const TracingOptions& options, std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter);
  Tracing(const Tracing&) = delete;
  Tracing& operator=(const Tracing&) = delete;
  Tracing(Tracing&&) = delete;
  Tracing& operator=(Tracing&&) = delete;
  ~Tracing();

  [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer(std::string_view scope) const;

  /// Export everything recorded so far; false when the exporter did not finish in time.
  bool flush();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

/// Ids of the span active on this thread, for log correlation (log::LoggerOptions::trace_context).
[[nodiscard]] std::optional<log::TraceIds> current_trace_ids();

}  // namespace psim::platform::observability
