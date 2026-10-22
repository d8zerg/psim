#include "metrics.hpp"

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>

#include <chrono>
#include <string>
#include <utility>

#include "psim/platform/kafka/client.hpp"
#include "psim/platform/kafka/trace_context.hpp"
#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/runtime.hpp"
#include "routes.hpp"

namespace psim::api_gateway {
HttpMetrics::HttpMetrics(const platform::runtime::Context& context) : context_(&context) {
  auto& registry = context.metrics().registry();
  auto& requests = prometheus::BuildCounter()
                       .Name("psim_api_requests_total")
                       .Help("REST requests by contract operation")
                       .Register(registry);
  auto& latency = prometheus::BuildHistogram()
                      .Name("psim_api_request_seconds")
                      .Help("REST operation duration including authorization and response streaming")
                      .Register(registry);
  const prometheus::Histogram::BucketBoundaries buckets{0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25,
                                                        0.5,   1,     2,    5,     15,   60};
  auto add = [&](const std::string& operation) {
    records_.emplace(operation, Record{&requests.Add({{"operation", operation}}),
                                       &latency.Add({{"operation", operation}}, buckets)});
  };
  for (const auto& route : routes()) {
    add(route.operation);
  }
  add("unmatched");
  active_ = &prometheus::BuildGauge()
                 .Name("psim_api_requests_active")
                 .Help("Active REST requests")
                 .Register(registry)
                 .Add({});
  denied_ = &prometheus::BuildCounter()
                 .Name("psim_api_access_denied_total")
                 .Help("Rejected API access decisions")
                 .Register(registry)
                 .Add({});
  limited_ = &prometheus::BuildCounter()
                  .Name("psim_api_rate_limited_total")
                  .Help("Requests and connections rejected by rate limits")
                  .Register(registry)
                  .Add({});
  proxy_failed_ = &prometheus::BuildCounter()
                       .Name("psim_api_proxy_failures_total")
                       .Help("Failed upstream exchanges or response delivery")
                       .Register(registry)
                       .Add({});
}

HttpMetrics::Request HttpMetrics::start(std::string operation, boost::beast::http::fields* fields) {
  if (!records_.contains(operation)) {
    operation = "unmatched";
  }
  platform::kafka::Headers incoming;
  for (const auto* name : {"traceparent", "tracestate"}) {
    const auto value = (*fields)[name];
    if (!value.empty()) {
      incoming.emplace_back(name, std::string(value));
    }
  }
  auto trace = platform::stream::span(*context_, "HTTP " + operation, incoming);
  platform::kafka::Headers outgoing;
  platform::kafka::inject(*trace, outgoing);
  fields->erase("traceparent");
  fields->erase("tracestate");
  for (const auto& [name, value] : outgoing) {
    fields->set(name, value);
  }
  return {this, std::move(operation), std::move(trace)};
}

HttpMetrics::Request::Request(HttpMetrics* owner, std::string operation, platform::stream::Span trace)
    : owner_(owner),
      operation_(std::move(operation)),
      trace_(std::move(trace)),
      started_(std::chrono::steady_clock::now()) {
  owner_->records_.at(operation_).requests->Increment();
  owner_->active_->Increment();
}

HttpMetrics::Request::~Request() {
  owner_->records_.at(operation_)
      .seconds->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count());
  owner_->active_->Decrement();
  trace_->End();
}
}  // namespace psim::api_gateway
