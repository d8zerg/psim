#pragma once

#include <boost/beast/http/fields.hpp>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>

#include <chrono>
#include <map>
#include <string>

#include "psim/platform/runtime.hpp"
#include "psim/platform/stream/runtime.hpp"

namespace psim::api_gateway {
class HttpMetrics {
 public:
  class Request {
   public:
    Request(HttpMetrics* owner, std::string operation, platform::stream::Span trace);
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    Request(Request&&) = delete;
    Request& operator=(Request&&) = delete;
    ~Request();

   private:
    HttpMetrics* owner_;
    std::string operation_;
    platform::stream::Span trace_;
    std::chrono::steady_clock::time_point started_;
  };

  explicit HttpMetrics(const platform::runtime::Context& context);
  Request start(std::string operation, boost::beast::http::fields* fields);

  void denied() { denied_->Increment(); }

  void limited() { limited_->Increment(); }

  void limit_failed() { limit_failed_->Increment(); }

  void proxy_failed() { proxy_failed_->Increment(); }

 private:
  struct Record {
    prometheus::Counter* requests;
    prometheus::Histogram* seconds;
  };

  const platform::runtime::Context* context_;
  std::map<std::string, Record> records_;
  prometheus::Gauge* active_;
  prometheus::Counter* denied_;
  prometheus::Counter* limited_;
  prometheus::Counter* limit_failed_;
  prometheus::Counter* proxy_failed_;
};
}  // namespace psim::api_gateway
