#pragma once
#include <map>

#include "psim/api/v1/feed.pb.h"
#include "psim/incident/v1/events.pb.h"
#include "psim/platform/pg/store.hpp"

namespace psim::api_gateway {
struct Snapshot {
  api::v1::GetIncidentFeedResponse feed;
  std::map<std::string, std::int64_t> offsets;
};

class Projection {
 public:
  Projection(platform::pg::Pool* pool, std::string replica)
      : pool_(pool), replica_(std::move(replica)), messages_("access") {}

  boost::asio::awaitable<platform::Result<void>> initialize();
  boost::asio::awaitable<platform::Result<void>> apply(incident::v1::IncidentEventRecord event,
                                                       const platform::kafka::Message* message);
  boost::asio::awaitable<platform::Result<Snapshot>> snapshot(std::string tenant);

 private:
  platform::pg::Pool* pool_;
  std::string replica_;
  platform::pg::MessageStore messages_;
};
}  // namespace psim::api_gateway
