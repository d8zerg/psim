#pragma once
#include <boost/beast/http.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "psim/platform/error.hpp"

namespace psim::api_gateway {
namespace http = boost::beast::http;

struct Route {
  http::verb method;
  std::string path;
  std::string service;
  std::string action;
  std::string operation;
  bool idempotency = false;
  bool precondition = false;
  bool floor_plan = false;
};

struct FeedFilter {
  std::set<std::string> sites;
  std::set<std::string> zones;
};

platform::Result<FeedFilter> feed_filter(std::string_view target, bool zones = true);
const std::vector<Route>& routes();
std::uint64_t request_limit(const Route& item);
platform::Result<const Route*> route(http::verb method, std::string_view target);
platform::Result<void> request_headers(const Route& item, const http::fields& fields);
void security_headers(http::fields& fields, std::string_view origin = {});
bool allowed_origin(std::string_view origin, const std::vector<std::string>& origins);

/// Shard-owned token buckets with a bounded number of identities and monotonic expiry.
class RateLimiter {
 public:
  RateLimiter(std::size_t rate, std::size_t burst, std::size_t capacity = 10000)
      : rate_(rate), burst_(burst), capacity_(capacity) {}

  bool allow(std::string key, std::chrono::steady_clock::time_point now);

 private:
  struct Bucket {
    double tokens;
    std::chrono::steady_clock::time_point updated;
  };

  std::size_t rate_, burst_, capacity_;
  std::map<std::string, Bucket> buckets_;
};
}  // namespace psim::api_gateway
