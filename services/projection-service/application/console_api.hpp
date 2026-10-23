#pragma once
#include <set>
#include <string>
#include <string_view>

#include "console_store.hpp"
#include "psim/platform/error.hpp"
#include "psim/platform/security/jwt.hpp"
#include "report.hpp"

namespace psim::projection {
struct FeedFilter {
  std::set<std::string> sites, zones;
};

platform::Result<FeedFilter> feed_filter(std::string_view target, bool zones = true);
boost::asio::awaitable<platform::Result<Response>> console_request(ConsoleStore* store, const Request* request,
                                                                   platform::security::Principal principal);
}  // namespace psim::projection
