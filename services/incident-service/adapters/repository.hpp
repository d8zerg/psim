#pragma once
#include <functional>

#include "psim/incident/v1/events.pb.h"
#include "psim/platform/pg/store.hpp"
#include "psim/processing/v1/signal.pb.h"

namespace psim::incident_service {
class Repository {
 public:
  explicit Repository(platform::pg::Pool* pool) : pool_(pool), messages_("incident") {}

  boost::asio::awaitable<platform::Result<void>> initialize();
  using Encode = std::function<platform::kafka::Record(const incident::v1::IncidentEventRecord&)>;
  boost::asio::awaitable<platform::Result<void>> apply(processing::v1::SignalRecord signal, Encode encode);

  platform::pg::MessageStore messages() const { return messages_; }

 private:
  platform::pg::Pool* pool_;
  platform::pg::MessageStore messages_;
};
}  // namespace psim::incident_service
