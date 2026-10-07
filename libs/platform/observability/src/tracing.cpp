#include "psim/platform/tracing.hpp"

#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_options.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/samplers/parent_factory.h>
#include <opentelemetry/sdk/trace/samplers/trace_id_ratio_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/noop.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/tracer.h>
#include <opentelemetry/trace/tracer_provider.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "psim/platform/log.hpp"

namespace psim::platform::observability {

namespace otel = opentelemetry;
namespace sdk = opentelemetry::sdk;

struct Tracing::State {
  std::shared_ptr<sdk::trace::TracerProvider> provider;
};

namespace {

std::unique_ptr<sdk::trace::SpanExporter> otlp_exporter(const TracingOptions& options) {
  otel::exporter::otlp::OtlpGrpcExporterOptions exporter_options;
  exporter_options.endpoint = options.otlp_endpoint;
  exporter_options.use_ssl_credentials = false;  // TLS to the Collector comes with step 2.6 (SR-08)
  return otel::exporter::otlp::OtlpGrpcExporterFactory::Create(exporter_options);
}

}  // namespace

Tracing::Tracing(const TracingOptions& options)
    : Tracing(options, options.otlp_endpoint.empty() ? nullptr : otlp_exporter(options)) {}

Tracing::Tracing(const TracingOptions& options, std::unique_ptr<sdk::trace::SpanExporter> exporter)
    : state_(std::make_unique<State>()) {
  const auto resource = sdk::resource::Resource::Create({{"service.name", options.service},
                                                         {"service.version", options.version},
                                                         {"service.instance.id", options.instance}});
  auto sampler = sdk::trace::ParentBasedSamplerFactory::Create(std::shared_ptr<sdk::trace::Sampler>(
      sdk::trace::TraceIdRatioBasedSamplerFactory::Create(options.sampling_ratio)));
  std::vector<std::unique_ptr<sdk::trace::SpanProcessor>> processors;
  if (exporter) {
    processors.push_back(
        sdk::trace::BatchSpanProcessorFactory::Create(std::move(exporter), sdk::trace::BatchSpanProcessorOptions{}));
  }
  state_->provider = sdk::trace::TracerProviderFactory::Create(std::move(processors), resource, std::move(sampler));
  otel::trace::Provider::SetTracerProvider(otel::nostd::shared_ptr<otel::trace::TracerProvider>(state_->provider));
}

Tracing::~Tracing() {
  (void)flush();
  state_->provider->Shutdown();
  otel::trace::Provider::SetTracerProvider(
      otel::nostd::shared_ptr<otel::trace::TracerProvider>(std::make_shared<otel::trace::NoopTracerProvider>()));
}

otel::nostd::shared_ptr<otel::trace::Tracer> Tracing::tracer(std::string_view scope) const {
  return state_->provider->GetTracer(otel::nostd::string_view(scope.data(), scope.size()));
}

bool Tracing::flush() {
  return state_->provider->ForceFlush(std::chrono::seconds(5));
}

std::optional<log::TraceIds> current_trace_ids() {
  const auto context = otel::trace::Tracer::GetCurrentSpan()->GetContext();
  if (!context.IsValid()) {
    return std::nullopt;
  }
  std::array<char, 32> trace{};
  std::array<char, 16> span{};
  context.trace_id().ToLowerBase16(trace);
  context.span_id().ToLowerBase16(span);
  return log::TraceIds{std::string(trace.data(), trace.size()), std::string(span.data(), span.size())};
}

}  // namespace psim::platform::observability
