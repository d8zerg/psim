#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>
#include <prometheus/counter.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>

#include "psim/api/v1/feed.pb.h"
#include "psim/platform/error.hpp"
#include "psim/platform/kafka/client.hpp"
#include "psim/platform/pg/connection.hpp"
#include "psim/platform/pg/pool.hpp"
#include "psim/platform/redis/pool.hpp"
#include "psim/platform/security/jwt.hpp"

namespace psim::projection {
struct StateSnapshot {
  api::v1::GetIncidentFeedResponse feed;
  api::v1::GetLocationStatusResponse location;
  std::string generation;
  api::v1::GetOperatorLoadResponse operators;
  std::map<std::string, std::int64_t> offsets;
  api::v1::GetOperationalSnapshotResponse history;
};

class ConsoleStore {
 public:
  ConsoleStore(platform::pg::Pool* pool, std::string replica, platform::redis::Pool* leases = nullptr,
               prometheus::Counter* hits = nullptr, prometheus::Counter* misses = nullptr)
      : pool_(pool), replica_(std::move(replica)), leases_(leases), hits_(hits), misses_(misses) {}

  void writer(platform::pg::Connection* connection) { writer_ = connection; }

  boost::asio::awaitable<platform::Result<void>> initialize();
  boost::asio::awaitable<platform::Result<std::string>> active();
  boost::asio::awaitable<platform::Result<void>> activate(bool compare);
  boost::asio::awaitable<platform::Result<void>> discard();
  boost::asio::awaitable<platform::Result<platform::kafka::Offsets>> positions();
  boost::asio::awaitable<platform::Result<void>> advance(platform::kafka::Offsets positions);
  boost::asio::awaitable<platform::Result<void>> apply(const platform::kafka::Message* message);
  boost::asio::awaitable<platform::Result<void>> apply_batch(std::span<const platform::kafka::Message> messages);
  boost::asio::awaitable<platform::Result<StateSnapshot>> snapshot(platform::security::Principal principal,
                                                                   std::string location,
                                                                   std::set<std::string> sites = {},
                                                                   std::optional<std::int64_t> at = {});

 private:
  platform::pg::Pool* pool_;
  platform::pg::Connection* writer_ = nullptr;
  std::string replica_;
  platform::redis::Pool* leases_;
  prometheus::Counter* hits_;
  prometheus::Counter* misses_;
};
}  // namespace psim::projection
