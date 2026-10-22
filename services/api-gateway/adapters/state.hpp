#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <set>
#include <string>

#include "psim/api/v1/feed.pb.h"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/redis/pool.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::api_gateway {
struct StateSnapshot {
  api::v1::GetLocationStatusResponse location;
  api::v1::GetOperatorLoadResponse operators;
  std::map<std::string, std::int64_t> offsets;
};

class StateProjection {
 public:
  StateProjection(platform::pg::Pool* pool, std::string replica, platform::redis::Pool* leases = nullptr)
      : pool_(pool), replica_(std::move(replica)), leases_(leases) {}

  boost::asio::awaitable<platform::Result<void>> initialize();
  boost::asio::awaitable<platform::Result<void>> apply(const platform::kafka::Message* message);
  boost::asio::awaitable<platform::Result<StateSnapshot>> snapshot(platform::security::Principal principal,
                                                                   std::string location,
                                                                   std::set<std::string> sites = {});

 private:
  platform::pg::Pool* pool_;
  std::string replica_;
  platform::redis::Pool* leases_;
};
}  // namespace psim::api_gateway
