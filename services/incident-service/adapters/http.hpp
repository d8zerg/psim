#pragma once
#include <boost/beast/http.hpp>
#include <prometheus/counter.h>
#include <prometheus/family.h>
#include <prometheus/histogram.h>

#include "repository.hpp"

namespace psim::incident_service {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

class Api {
 public:
  explicit Api(Repository* repository, prometheus::Family<prometheus::Counter>* operations = nullptr,
               prometheus::Family<prometheus::Histogram>* latency = nullptr)
      : repository_(repository), operations_(operations), latency_(latency) {}

  asio::awaitable<p::Result<Response>> handle(Request request, p::security::Principal principal);
  static Response problem(const p::Error& error);

 private:
  Repository* repository_;
  prometheus::Family<prometheus::Counter>* operations_;
  prometheus::Family<prometheus::Histogram>* latency_;
};
}  // namespace psim::incident_service
