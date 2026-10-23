#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "psim/platform/kafka/consumer.hpp"
#include "psim/platform/security/jwt.hpp"
#include "psim/realtime/v1/realtime.pb.h"

namespace psim::api_gateway {
inline constexpr auto kIncidentTopic = "psim.incidents.events.v1";
inline constexpr auto kResponseTopic = "psim.response.events.v1";

struct RealtimeEvent {
  realtime::v1::Delta value;
  std::string tenant, site, zone, incident;
  platform::kafka::TopicPartition partition;
  std::int64_t offset = 0;
  std::chrono::system_clock::time_point timestamp;
};

platform::Result<RealtimeEvent> realtime_event(const platform::kafka::Message& message);
std::string scope_key(const platform::security::Principal& principal);
std::string resume_token(const platform::kafka::Offsets& positions, std::string tenant);
platform::Result<platform::kafka::Offsets> resume_positions(std::string_view token, std::string_view tenant);
bool realtime_filter(const platform::security::Principal& principal, const realtime::v1::Subscribe& subscription,
                     const RealtimeEvent& event);
boost::asio::awaitable<platform::Result<platform::kafka::Offsets>> replay_events(
    platform::kafka::Consumer* source, platform::kafka::Offsets positions,
    std::function<void(const RealtimeEvent&)> receive);
}  // namespace psim::api_gateway
