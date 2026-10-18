#pragma once

#include <psim/catalog/v1/catalog.pb.h>
#include <psim/connector/v1/connector.pb.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "psim/platform/error.hpp"

namespace psim::connector_gateway {
using Time = std::chrono::system_clock::time_point;

struct Certificate {
  std::string identity;
  std::string fingerprint;
  Time expires_at;
};

struct Registration {
  std::string tenant;
  catalog::v1::Connector connector{};
};

[[nodiscard]] bool authorized(const Registration& registration, const Certificate& certificate, Time now);
[[nodiscard]] platform::Result<void> validate(const connector::v1::ConnectorEvent& event);
[[nodiscard]] platform::Result<void> validate(const connector::v1::StateReport& report);
[[nodiscard]] std::string protocol(const connector::v1::Register& registration);
[[nodiscard]] bool supports(const connector::v1::Register& registration, std::string_view capability);
[[nodiscard]] bool recent(const google::protobuf::Timestamp& timestamp, Time now, std::chrono::seconds age);
[[nodiscard]] bool live(const connector::v1::ConnectorEvent& event, std::string_view session, Time now);
}  // namespace psim::connector_gateway
