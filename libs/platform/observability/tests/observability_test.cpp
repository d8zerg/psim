#include <gtest/gtest.h>
#include <opentelemetry/exporters/memory/in_memory_span_data.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/common/attribute_utils.h>
#include <opentelemetry/trace/scope.h>
#include <prometheus/counter.h>

#include <memory>
#include <string>
#include <utility>

#include "psim/platform/log.hpp"
#include "psim/platform/metrics.hpp"
#include "psim/platform/tracing.hpp"

namespace {

using opentelemetry::exporter::memory::InMemorySpanExporter;
using psim::platform::observability::current_trace_ids;
using psim::platform::observability::Metrics;
using psim::platform::observability::Tracing;
using psim::platform::observability::TracingOptions;

TEST(Metrics, ExposesRegisteredFamiliesInTheTextFormat) {
  Metrics metrics;
  auto& family = prometheus::BuildCounter()
                     .Name("psim_test_events_total")
                     .Help("Events seen by the test")
                     .Register(metrics.registry());
  family.Add({{"stage", "ingest"}}).Increment(3);

  const auto text = metrics.expose();

  EXPECT_NE(text.find("# TYPE psim_test_events_total counter"), std::string::npos) << text;
  EXPECT_NE(text.find(R"(psim_test_events_total{stage="ingest"} 3)"), std::string::npos) << text;
}

TracingOptions options(double ratio) {
  return TracingOptions{.service = "normalizer",
                        .version = "0.1.0",
                        .instance = "normalizer-0",
                        .otlp_endpoint = "",
                        .sampling_ratio = ratio};
}

TEST(Tracing, ExportsSpansWithTheServiceResource) {
  auto exporter = std::make_unique<InMemorySpanExporter>();
  const auto data = exporter->GetData();
  Tracing tracing(options(1.0), std::move(exporter));

  auto span = tracing.tracer("test")->StartSpan("normalize batch");
  {
    const opentelemetry::trace::Scope scope(span);
    const auto ids = current_trace_ids().value_or(psim::platform::log::TraceIds{});
    EXPECT_EQ(ids.trace_id.size(), 32U);
    EXPECT_EQ(ids.span_id.size(), 16U);
  }
  span->End();
  ASSERT_TRUE(tracing.flush());

  const auto spans = data->GetSpans();
  ASSERT_EQ(spans.size(), 1U);
  EXPECT_EQ(std::string(spans[0]->GetName()), "normalize batch");
  const auto& attributes = spans[0]->GetResource().GetAttributes();
  EXPECT_EQ(opentelemetry::nostd::get<std::string>(attributes.at("service.name")), "normalizer");
  EXPECT_EQ(opentelemetry::nostd::get<std::string>(attributes.at("service.instance.id")), "normalizer-0");
}

TEST(Tracing, HasNoTraceContextOutsideASpan) {
  EXPECT_FALSE(current_trace_ids().has_value());
}

TEST(Tracing, SamplesNewTracesByRatio) {
  auto exporter = std::make_unique<InMemorySpanExporter>();
  const auto data = exporter->GetData();
  Tracing tracing(options(0.0), std::move(exporter));

  tracing.tracer("test")->StartSpan("dropped")->End();
  ASSERT_TRUE(tracing.flush());

  EXPECT_TRUE(data->GetSpans().empty());
}

TEST(Tracing, WithoutAnEndpointRecordsNothingAndStillWorks) {
  Tracing tracing(options(1.0));
  tracing.tracer("test")->StartSpan("local only")->End();
  EXPECT_TRUE(tracing.flush());
}

}  // namespace
